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
            throw new InvalidArgumentException('Tanggal pemeriksaan tidak boleh sebelum tanggal lahir.');
        }
        return (int)$birth->diff($measured)->days;
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
            throw new DomainException('Usia balita harus 24 sampai kurang dari 60 bulan.');
        }
        if ($heightCm < 50 || $heightCm > 150) {
            throw new DomainException('Tinggi badan harus berada pada rentang 50–150 cm.');
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
            throw new RuntimeException("Referensi WHO tidak ditemukan untuk umur {$ageDays} hari.");
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

