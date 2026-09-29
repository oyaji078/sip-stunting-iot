<?php

declare(strict_types=1);

final class ApiController
{
    private array $device;

    public function __construct(private PDO $pdo)
    {
        $this->authenticate();
    }

    public function dispatch(string $endpoint): never
    {
        if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') {
            json_response(['status' => 'error', 'error' => ['code' => 'method_not_allowed', 'message' => 'Gunakan metode POST.']], 405);
        }
        $payload = json_decode(file_get_contents('php://input') ?: '{}', true);
        if (!is_array($payload)) {
            json_response(['status' => 'error', 'error' => ['code' => 'invalid_json', 'message' => 'JSON tidak valid.']], 400);
        }

        match ($endpoint) {
            'heartbeat' => $this->heartbeat($payload),
            'rfid/lookup' => $this->lookup($payload),
            'measurements' => $this->measurement($payload),
            default => json_response(['status' => 'error', 'error' => ['code' => 'not_found', 'message' => 'Endpoint tidak ditemukan.']], 404),
        };
    }

    private function authenticate(): void
    {
        $deviceUid = trim($_SERVER['HTTP_X_DEVICE_ID'] ?? '');
        $auth = $_SERVER['HTTP_AUTHORIZATION'] ?? '';
        if (!preg_match('/^Bearer\s+(.+)$/i', $auth, $matches) || $deviceUid === '') {
            json_response(['status' => 'error', 'error' => ['code' => 'unauthorized', 'message' => 'Identitas atau token perangkat tidak tersedia.']], 401);
        }
        $stmt = $this->pdo->prepare('SELECT * FROM devices WHERE device_uid = ? LIMIT 1');
        $stmt->execute([$deviceUid]);
        $device = $stmt->fetch();
        if (!$device || !hash_equals($device['token_hash'], hash('sha256', $matches[1]))) {
            json_response(['status' => 'error', 'error' => ['code' => 'unauthorized', 'message' => 'Token perangkat tidak valid.']], 401);
        }
        if (!(bool)$device['is_active']) {
            json_response(['status' => 'error', 'error' => ['code' => 'device_inactive', 'message' => 'Perangkat dinonaktifkan.']], 403);
        }
        $this->device = $device;
        $this->touchDevice();
    }

    private function touchDevice(?array $payload = null): void
    {
        $firmware = $payload['firmware_version'] ?? $this->device['firmware_version'];
        $rssi = isset($payload['wifi_rssi']) ? (int)$payload['wifi_rssi'] : $this->device['wifi_rssi'];
        $stmt = $this->pdo->prepare('UPDATE devices SET firmware_version = ?, wifi_rssi = ?, last_ip = ?, last_seen_at = NOW() WHERE id = ?');
        $stmt->execute([$firmware, $rssi, $_SERVER['REMOTE_ADDR'] ?? null, $this->device['id']]);
    }

    private function heartbeat(array $payload): never
    {
        $this->touchDevice($payload);
        json_response(['status' => 'success', 'server_time' => date(DATE_ATOM), 'device' => ['id' => $this->device['device_uid'], 'name' => $this->device['name']]]);
    }

    private function lookup(array $payload): never
    {
        try {
            $uid = normalize_uid((string)($payload['uid'] ?? ''));
        } catch (InvalidArgumentException $e) {
            json_response(['status' => 'error', 'error' => ['code' => 'invalid_uid', 'message' => $e->getMessage()]], 422);
        }
        $stmt = $this->pdo->prepare('SELECT * FROM children WHERE rfid_uid = ? LIMIT 1');
        $stmt->execute([$uid]);
        $child = $stmt->fetch();
        if (!$child) {
            $stmt = $this->pdo->prepare('INSERT INTO unknown_rfid_scans (rfid_uid, device_id) VALUES (?, ?) ON DUPLICATE KEY UPDATE scan_count = scan_count + 1, last_scanned_at = NOW()');
            $stmt->execute([$uid, $this->device['id']]);
            $this->recordScan($uid, 'unknown', null);
            json_response(['status' => 'unknown', 'uid' => $uid], 404);
        }
        if (!(bool)$child['is_active']) {
            $this->recordScan($uid, 'inactive', (int)$child['id']);
            json_response(['status' => 'inactive', 'message' => 'Data balita tidak aktif.'], 409);
        }
        $today = date('Y-m-d');
        if (!GrowthService::isEligible($child['birth_date'], $today)) {
            $this->recordScan($uid, 'ineligible', (int)$child['id']);
            json_response(['status' => 'ineligible', 'message' => 'Usia balita harus 24 sampai kurang dari 60 bulan.'], 422);
        }
        $this->recordScan($uid, 'registered', (int)$child['id']);
        json_response(['status' => 'registered', 'child' => [
            'name' => $child['name'], 'sex' => $child['sex'],
            'birth_date' => $child['birth_date'], 'age_days' => GrowthService::ageInDays($child['birth_date'], $today),
        ]]);
    }

    /**
     * Simpan setiap tap RFID agar operator bisa "tempel RFID" dari web
     * (pendaftaran balita & pemilihan balita saat pemeriksaan). Dibungkus
     * try/catch supaya lookup perangkat tetap jalan bila tabel rfid_scans
     * belum dibuat (setup belum dijalankan ulang).
     */
    private function recordScan(string $uid, string $outcome, ?int $childId): void
    {
        try {
            $stmt = $this->pdo->prepare('INSERT INTO rfid_scans (device_id, rfid_uid, outcome, child_id) VALUES (?, ?, ?, ?)');
            $stmt->execute([$this->device['id'], $uid, $outcome, $childId]);
            $this->pdo->exec('DELETE FROM rfid_scans WHERE scanned_at < DATE_SUB(NOW(), INTERVAL 1 DAY)');
        } catch (Throwable $e) {
            // Tabel belum ada atau gagal menulis: abaikan, jangan ganggu alur perangkat.
        }
    }

    private function measurement(array $payload): never
    {
        try {
            $uid = normalize_uid((string)($payload['uid'] ?? ''));
            $requestId = trim((string)($payload['request_id'] ?? ''));
            $weight = filter_var($payload['weight_kg'] ?? null, FILTER_VALIDATE_FLOAT);
            $height = filter_var($payload['height_cm'] ?? null, FILTER_VALIDATE_FLOAT);
            if (!preg_match('/^[A-Za-z0-9._:-]{8,80}$/', $requestId)) throw new DomainException('request_id tidak valid.');
            if ($weight === false || $weight < 2 || $weight > 100) throw new DomainException('Berat harus berada pada rentang 2–100 kg.');
            if ($height === false || $height < 50 || $height > 150) throw new DomainException('Tinggi harus berada pada rentang 50–150 cm.');

            $dupe = $this->pdo->prepare('SELECT id, z_score, category FROM measurements WHERE device_id = ? AND request_id = ?');
            $dupe->execute([$this->device['id'], $requestId]);
            if ($existing = $dupe->fetch()) {
                json_response(['status' => 'success', 'duplicate' => true, 'measurement_id' => (int)$existing['id'], 'z_score' => (float)$existing['z_score'], 'category' => $existing['category']]);
            }

            $stmt = $this->pdo->prepare('SELECT * FROM children WHERE rfid_uid = ? AND is_active = 1 LIMIT 1');
            $stmt->execute([$uid]);
            $child = $stmt->fetch();
            if (!$child) json_response(['status' => 'error', 'error' => ['code' => 'child_not_found', 'message' => 'RFID balita tidak terdaftar atau tidak aktif.']], 404);

            $measuredAt = date('Y-m-d H:i:s');
            $growth = (new GrowthService($this->pdo))->calculate($child['sex'], $child['birth_date'], substr($measuredAt, 0, 10), (float)$height);
            $insert = $this->pdo->prepare('INSERT INTO measurements (request_id, child_id, device_id, source, status, measured_at, age_days, weight_kg, height_cm, z_score, category, reference_version) VALUES (?, ?, ?, \'iot\', \'pending\', ?, ?, ?, ?, ?, ?, ?)');
            $insert->execute([$requestId, $child['id'], $this->device['id'], $measuredAt, $growth['age_days'], $weight, $height, $growth['z_score'], $growth['category'], $growth['reference_version']]);
            json_response(['status' => 'success', 'duplicate' => false, 'measurement_id' => (int)$this->pdo->lastInsertId(), 'z_score' => $growth['z_score'], 'category' => $growth['category']]);
        } catch (DomainException|InvalidArgumentException $e) {
            json_response(['status' => 'error', 'error' => ['code' => 'validation_error', 'message' => $e->getMessage()]], 422);
        } catch (Throwable $e) {
            error_log('Measurement API error: ' . $e->getMessage());
            json_response(['status' => 'error', 'error' => ['code' => 'server_error', 'message' => 'Pemeriksaan gagal diproses.']], 500);
        }
    }
}
