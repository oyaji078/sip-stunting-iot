<?php

declare(strict_types=1);

final class GrowthService
{
    public const REFERENCE_VERSION = 'WHO-CGS-2006-anthro-1.1.0-standing';

    public function __construct(private PDO $pdo)
    {
    }

    public static function ageInDays(string $birthDate, string $measuredDate): int
    {
        $birth = new DateTimeImmutable($birthDate);
        $measured = new DateTimeImmutable($measuredDate);
        if ($measured < $birth) {
            throw new InvalidArgumentException('Tanggal Pemeriksaan Tidak Boleh Sebelum Tanggal Lahir.');
        }
        return (int)$birth->diff($measured)->days;
    }

    /**
     * Umur dalam bulan kalender penuh (28 Feb -> 28 Mar = 1 bulan). Dipakai
     * tabel balita web dan layar LCD alat agar angkanya selalu sama.
     */
    public static function ageInMonths(string $birthDate, string $onDate): int
    {
        $birth = new DateTimeImmutable($birthDate);
        $on = new DateTimeImmutable($onDate);
        if ($on < $birth) {
            return 0;
        }
        $diff = $birth->diff($on);
        return $diff->y * 12 + $diff->m;
    }

    public static function isEligible(string $birthDate, string $measuredDate): bool
    {
        $birth = new DateTimeImmutable($birthDate);
        $measured = new DateTimeImmutable($measuredDate);
        return $measured >= $birth->modify('+2 years') && $measured < $birth->modify('+5 years');
    }

    public function calculate(string $sex, string $birthDate, string $measuredDate, float $heightCm): array
    {
        if (!self::isEligible($birthDate, $measuredDate)) {
            throw new DomainException('Usia Balita Harus 24 Sampai Kurang Dari 60 Bulan.');
        }
        if ($heightCm < 50 || $heightCm > 150) {
            throw new DomainException('Tinggi Badan Harus Berada Pada Rentang 50–150 cm.');
        }

        $ageDays = self::ageInDays($birthDate, $measuredDate);
        $sexCode = strtoupper($sex) === 'L' ? 1 : 2;

        // Alat selalu mengukur tinggi berdiri. WHO Anthro memakai referensi
        // panjang terlentang sebelum 731 hari, sehingga tinggi berdiri pada
        // kasus batas tersebut harus ditambah 0,7 cm terlebih dahulu.
        $measurementType = $ageDays < 731 ? 'L' : 'H';
        $standardizedHeightCm = $ageDays < 731 ? $heightCm + 0.7 : $heightCm;

        $stmt = $this->pdo->prepare('SELECT l, m, s FROM who_lms WHERE sex = ? AND age_days = ? AND measurement_type = ? LIMIT 1');
        $stmt->execute([$sexCode, $ageDays, $measurementType]);
        $row = $stmt->fetch();
        if (!$row) {
            throw new RuntimeException("Referensi WHO Tidak Ditemukan Untuk Umur {$ageDays} Hari.");
        }

        $l = (float)$row['l'];
        $m = (float)$row['m'];
        $s = (float)$row['s'];
        $z = abs($l) < 1e-12
            ? log($standardizedHeightCm / $m) / $s
            : (pow($standardizedHeightCm / $m, $l) - 1) / ($l * $s);

        return [
            'age_days' => $ageDays,
            'z_score' => round($z, 4),
            'category' => self::category($z),
            'reference_version' => self::REFERENCE_VERSION,
        ];
    }

    public static function category(float $z): string
    {
        return match (true) {
            $z < -3 => 'Sangat Pendek',
            $z < -2 => 'Pendek/Stunting',
            $z <= 3 => 'Normal',
            default => 'Tinggi',
        };
    }
}

