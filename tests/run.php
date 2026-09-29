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

echo "OK - {$tests} pengujian lulus\n";

