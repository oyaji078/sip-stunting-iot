<?php

declare(strict_types=1);

require dirname(__DIR__) . '/app/GrowthService.php';
require dirname(__DIR__) . '/app/functions.php';

$pdo = new PDO('sqlite::memory:');
$pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
$pdo->exec('CREATE TABLE who_lms (sex INTEGER, age_days INTEGER, l REAL, m REAL, s REAL, measurement_type TEXT, PRIMARY KEY(sex,age_days))');
$insert = $pdo->prepare('INSERT INTO who_lms VALUES (?,?,?,?,?,?)');
$file = new SplFileObject(dirname(__DIR__) . '/database/who_lenanthro.tsv');
$file->fgets();
while (!$file->eof()) {
    $row = str_getcsv(trim($file->fgets()), "\t");
    if (count($row) === 6) $insert->execute($row);
}

$tests = 0;
$assert = static function (bool $condition, string $message) use (&$tests): void {
    $tests++;
    if (!$condition) throw new RuntimeException("GAGAL: {$message}");
};

$growth = new GrowthService($pdo);
$assert(GrowthService::ageInDays('2024-02-29', '2026-02-28') === 730, 'umur hari melewati tahun kabisat');
$assert(GrowthService::isEligible('2024-01-01', '2026-01-01'), 'tepat 24 bulan diterima');
$assert(!GrowthService::isEligible('2024-01-01', '2025-12-31'), 'sebelum 24 bulan ditolak');
$assert(!GrowthService::isEligible('2021-01-01', '2026-01-01'), 'tepat 5 tahun ditolak');
$assert(normalize_uid('a1-b2-c3-d4') === 'A1:B2:C3:D4', 'normalisasi UID');
$sensorData = device_sensor_data('{"rfid":true,"weight":false,"height":true,"lcd":true,"uptime_seconds":3661}');
$assert($sensorData['rfid'] === true && $sensorData['weight'] === false, 'status kesehatan sensor');
$assert($sensorData['uptime_seconds'] === 3661, 'uptime perangkat');
$assert(format_uptime(3661) === '1 Jam 1 Menit', 'format uptime perangkat');
$assert(time_ago(5) === '5 Detik Lalu', 'format waktu relatif');
$assert(tanggal_indonesia(strtotime('2026-10-03')) === 'Sabtu, 03 Oktober 2026', 'tanggal Indonesia');
$assert(tanggal_indonesia(strtotime('2026-01-04')) === 'Minggu, 04 Januari 2026', 'hari Minggu dan bulan Januari');
$assert(tanggal_indonesia(strtotime('2026-12-31')) === 'Kamis, 31 Desember 2026', 'bulan Desember');

// Umur dalam bulan kalender penuh: dipakai tabel web dan LCD alat.
$assert(GrowthService::ageInMonths('2024-01-15', '2026-01-15') === 24, 'tepat 24 bulan');
$assert(GrowthService::ageInMonths('2024-01-15', '2026-01-14') === 23, 'sehari sebelum 24 bulan');
$assert(GrowthService::ageInMonths('2021-03-31', '2026-03-30') === 59, 'hampir 60 bulan');
$assert(GrowthService::ageInMonths('2024-01-15', '2023-01-01') === 0, 'tanggal sebelum lahir menjadi 0');

foreach ([['L', 730], ['P', 730], ['L', 1461], ['P', 1825]] as [$sex, $age]) {
    $stmt = $pdo->prepare('SELECT l,m,s FROM who_lms WHERE sex=? AND age_days=?');
    $stmt->execute([$sex === 'L' ? 1 : 2, $age]);
    $ref = $stmt->fetch(PDO::FETCH_ASSOC);
    $birth = (new DateTimeImmutable('2020-01-01'))->format('Y-m-d');
    $date = (new DateTimeImmutable($birth))->modify("+{$age} days")->format('Y-m-d');
    if (!GrowthService::isEligible($birth, $date)) continue;
    foreach ([-3.2, -2.5, 0.0, 3.2] as $expectedZ) {
        $height = (float)$ref['m'] * pow(1 + (float)$ref['l'] * (float)$ref['s'] * $expectedZ, 1 / (float)$ref['l']);
        $result = $growth->calculate($sex, $birth, $date, $height);
        $assert(abs($result['z_score'] - $expectedZ) < 0.001, "LMS WHO {$sex}/{$age}/{$expectedZ}");
    }
}

$assert(GrowthService::category(-3.0001) === 'Sangat Pendek', 'batas sangat pendek');
$assert(GrowthService::category(-3.0) === 'Pendek/Stunting', 'batas -3');
$assert(GrowthService::category(-2.0) === 'Normal', 'batas -2');
$assert(GrowthService::category(3.0) === 'Normal', 'batas +3');
$assert(GrowthService::category(3.0001) === 'Tinggi', 'batas tinggi');

// WHO Anthro: alat mengukur berdiri. Pada usia < 731 hari, tinggi berdiri
// distandarkan menjadi panjang dengan tambahan 0,7 cm sebelum LMS dihitung.
$stmt = $pdo->prepare("SELECT m FROM who_lms WHERE sex=1 AND age_days=730 AND measurement_type='L'");
$stmt->execute();
$lengthMedian730 = (float)$stmt->fetchColumn();
$boundary730 = $growth->calculate('L', '2024-03-01', '2026-03-01', $lengthMedian730 - 0.7);
$assert(abs($boundary730['z_score']) < 0.0001, 'tinggi berdiri usia 730 hari dikoreksi +0,7 cm');
$stmt = $pdo->prepare("SELECT m FROM who_lms WHERE sex=1 AND age_days=731 AND measurement_type='H'");
$stmt->execute();
$heightMedian731 = (float)$stmt->fetchColumn();
$boundary731 = $growth->calculate('L', '2023-03-01', '2025-03-01', $heightMedian731);
$assert(abs($boundary731['z_score']) < 0.0001, 'usia 731 hari memakai referensi tinggi berdiri');
$assert($boundary731['reference_version'] === 'WHO-CGS-2006-anthro-1.1.0-standing', 'versi referensi WHO tercatat');

// Nilai jangkar dari contoh resmi paket WorldHealthOrganization/anthro:
// laki-laki 1000 hari/100 cm = 1,70 dan perempuan 1000 hari/80 cm = -3,50.
$anchorBirth = new DateTimeImmutable('2020-01-01');
$anchorDate = $anchorBirth->modify('+1000 days')->format('Y-m-d');
$anchorMale = $growth->calculate('L', $anchorBirth->format('Y-m-d'), $anchorDate, 100.0);
$anchorFemale = $growth->calculate('P', $anchorBirth->format('Y-m-d'), $anchorDate, 80.0);
$assert(round($anchorMale['z_score'], 2) === 1.70, 'jangkar resmi WHO laki-laki 1000 hari');
$assert(round($anchorFemale['z_score'], 2) === -3.50, 'jangkar resmi WHO perempuan 1000 hari');

// Redirect: path kosong harus ke beranda, bukan header Location kosong
// (bug yang membuat login berhasil tetapi tetap berada di halaman masuk).
$config = ['app' => ['url' => 'https://posyandu.oypoint.online']];
$assert(redirect_target('') === 'https://posyandu.oypoint.online/', 'redirect path kosong menuju beranda');
$assert(redirect_target('') !== '', 'redirect path kosong tidak boleh kosong');
$assert(redirect_target('login') === 'https://posyandu.oypoint.online/login', 'redirect path biasa');
$assert(redirect_target('ubah-password') === 'https://posyandu.oypoint.online/ubah-password', 'redirect ubah-password');
$assert(redirect_target('https://contoh.test/x') === 'https://contoh.test/x', 'redirect URL absolut tidak diubah');
$config = ['app' => ['url' => 'https://contoh.test/sub/']];
$assert(redirect_target('') === 'https://contoh.test/sub/', 'redirect beranda dengan subfolder');

echo "OK - {$tests} pengujian lulus\n";

