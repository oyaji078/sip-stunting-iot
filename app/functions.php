<?php

declare(strict_types=1);

function e(mixed $value): string
{
    return htmlspecialchars((string)$value, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
}

function url(string $path = ''): string
{
    global $config;
    return rtrim($config['app']['url'], '/') . '/' . ltrim($path, '/');
}

function asset_url(string $path): string
{
    $path = ltrim($path, '/');
    $file = dirname(__DIR__) . '/public/' . $path;
    $version = is_file($file) ? (string)filemtime($file) : '1';
    return url($path) . '?v=' . rawurlencode($version);
}

/**
 * Tujuan redirect. Path kosong berarti beranda (dashboard). Sebelumnya path
 * kosong dikirim apa adanya sebagai "Location: " kosong, sehingga setelah login
 * berhasil browser tetap berada di halaman masuk.
 */
function redirect_target(string $path): string
{
    return str_starts_with($path, 'http') ? $path : url($path);
}

function redirect(string $path): never
{
    header('Location: ' . redirect_target($path));
    exit;
}

function csrf_token(): string
{
    if (empty($_SESSION['csrf'])) {
        $_SESSION['csrf'] = bin2hex(random_bytes(32));
    }
    return $_SESSION['csrf'];
}

function csrf_field(): string
{
    return '<input type="hidden" name="csrf" value="' . e(csrf_token()) . '">';
}

function verify_csrf(): void
{
    if (!hash_equals($_SESSION['csrf'] ?? '', (string)($_POST['csrf'] ?? ''))) {
        http_response_code(419);
        exit('Sesi Formulir Kedaluwarsa. Muat Ulang Halaman Dan Coba Lagi.');
    }
}

function current_user(): ?array
{
    global $pdo;
    if (empty($_SESSION['user_id'])) {
        return null;
    }
    $stmt = $pdo->prepare('SELECT id, name, username, role, must_change_password FROM users WHERE id = ? AND is_active = 1');
    $stmt->execute([$_SESSION['user_id']]);
    return $stmt->fetch() ?: null;
}

function require_auth(?string $role = null): array
{
    $user = current_user();
    if (!$user) {
        redirect('login');
    }
    if ($role && $user['role'] !== $role) {
        http_response_code(403);
        exit('Anda Tidak Memiliki Hak Akses Untuk Tindakan Ini.');
    }
    return $user;
}

function flash(string $type, string $message): void
{
    $_SESSION['flash'][] = compact('type', 'message');
}

function pull_flashes(): array
{
    $messages = $_SESSION['flash'] ?? [];
    unset($_SESSION['flash']);
    return $messages;
}

function audit(string $action, ?string $entityType = null, ?int $entityId = null, array $details = []): void
{
    global $pdo;
    $stmt = $pdo->prepare('INSERT INTO audit_logs (user_id, action, entity_type, entity_id, details_json, ip_address, created_at) VALUES (?, ?, ?, ?, ?, ?, NOW())');
    $stmt->execute([
        $_SESSION['user_id'] ?? null,
        $action,
        $entityType,
        $entityId,
        $details ? json_encode($details, JSON_UNESCAPED_UNICODE) : null,
        $_SERVER['REMOTE_ADDR'] ?? null,
    ]);
}

function normalize_uid(string $uid): string
{
    $hex = strtoupper(preg_replace('/[^0-9A-Fa-f]/', '', $uid));
    if ($hex === '' || strlen($hex) % 2 !== 0 || strlen($hex) > 20) {
        throw new InvalidArgumentException('UID RFID Tidak Valid.');
    }
    return implode(':', str_split($hex, 2));
}

/**
 * Status online/offline dihitung dari selisih detik yang berasal dari jam
 * database (TIMESTAMPDIFF terhadap NOW()), bukan dari jam PHP. Ini menghindari
 * bug ketika timezone server MySQL berbeda dengan timezone aplikasi sehingga
 * perangkat yang baru saja heartbeat (HTTP 200) tetap tampil "Offline".
 */
function device_status(?int $secondsAgo): array
{
    if ($secondsAgo === null) {
        return ['Belum Aktif', 'secondary'];
    }
    return $secondsAgo <= 90 ? ['Online', 'success'] : ['Offline', 'danger'];
}

function time_ago(?int $seconds): string
{
    if ($seconds === null) {
        return 'Belum Pernah';
    }
    if ($seconds < 0) {
        $seconds = 0;
    }
    if ($seconds < 60) {
        return $seconds . ' Detik Lalu';
    }
    if ($seconds < 3600) {
        return intdiv($seconds, 60) . ' Menit Lalu';
    }
    if ($seconds < 86400) {
        return intdiv($seconds, 3600) . ' Jam Lalu';
    }
    return intdiv($seconds, 86400) . ' Hari Lalu';
}

function device_sensor_data(mixed $json): array
{
    $defaults = ['rfid' => null, 'weight' => null, 'height' => null, 'lcd' => null, 'uptime_seconds' => 0];
    if (!is_string($json) || $json === '') {
        return $defaults;
    }
    $decoded = json_decode($json, true);
    if (!is_array($decoded)) {
        return $defaults;
    }
    foreach (['rfid', 'weight', 'height', 'lcd'] as $key) {
        if (array_key_exists($key, $decoded)) {
            $defaults[$key] = (bool)$decoded[$key];
        }
    }
    $defaults['uptime_seconds'] = max(0, (int)($decoded['uptime_seconds'] ?? 0));
    return $defaults;
}

function format_uptime(int $seconds): string
{
    if ($seconds < 60) return $seconds . ' Detik';
    if ($seconds < 3600) return intdiv($seconds, 60) . ' Menit';
    if ($seconds < 86400) return intdiv($seconds, 3600) . ' Jam ' . intdiv($seconds % 3600, 60) . ' Menit';
    return intdiv($seconds, 86400) . ' Hari ' . intdiv($seconds % 86400, 3600) . ' Jam';
}

function rfid_scan_modal(): string
{
    $scanUrl = e(url('rfid/scan'));
    $modeUrl = e(url('rfid/mode'));
    $csrf = e(csrf_token());
    return <<<HTML
<div class="modal fade" id="rfidModal" tabindex="-1" data-scan-url="{$scanUrl}" data-mode-url="{$modeUrl}" data-csrf="{$csrf}"><div class="modal-dialog modal-dialog-centered"><div class="modal-content"><div class="modal-header"><h5 class="modal-title">Tempelkan Kartu RFID</h5><button type="button" class="btn-close" data-bs-dismiss="modal"></button></div><div class="modal-body text-center"><div class="rfid-wait" data-rfid-icon>⌁</div><p class="mb-1" data-rfid-status>Silakan Tempelkan Kartu RFID Pada Alat…</p><small class="text-muted">Alat Harus Online Dan Terhubung Ke Internet.</small></div></div></div></div>
HTML;
}

/**
 * Identitas Posyandu untuk judul halaman, sidebar, dan halaman masuk.
 * Nilai bawaan ada di kode; bisa ditimpa lewat config app.brand_name / app.brand_place.
 */
function app_brand(): array
{
    global $config;
    $name = trim((string)($config['app']['brand_name'] ?? '')) ?: 'Posyandu Ingin Sehat';
    $place = trim((string)($config['app']['brand_place'] ?? '')) ?: 'Dusun Dames Burneo';
    // Kata pertama jadi baris atas ("Posyandu"), sisanya baris kedua ("Ingin Sehat").
    $parts = preg_split('/\s+/', $name, 2);
    return [
        'name' => $name, 'place' => $place,
        'line1' => $parts[0], 'line2' => $parts[1] ?? '',
        'initial' => mb_strtoupper(mb_substr($name, 0, 1)),
    ];
}

function render(string $title, callable $content, string $active = ''): void
{
    global $config;
    $brand = app_brand();
    $user = current_user();
    $flashes = pull_flashes();
    ?><!doctype html>
<html lang="id"><head>
    <meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
    <title><?= e($title) ?> · <?= e($brand['name']) ?> <?= e($brand['place']) ?></title>
    <link rel="icon" type="image/png" href="<?= e(asset_url('assets/logo.png')) ?>">
    <link rel="stylesheet" href="<?= e(url('assets/vendor/bootstrap.min.css')) ?>">
    <link rel="stylesheet" href="<?= e(asset_url('assets/app.css')) ?>">
</head><body<?= $user && $active !== 'alat' ? ' data-live-url="' . e(url('live/version')) . '"' : '' ?>>
<?php if ($user): ?>
<div class="app-shell">
<aside class="sidebar">
    <a class="brand" href="<?= e(url()) ?>"><img class="brand-logo" src="<?= e(asset_url('assets/logo.png')) ?>" alt="Logo <?= e($brand['name']) ?>"><span class="brand-text"><b><?= e($brand['line1']) ?></b><?php if ($brand['line2'] !== ''): ?><b><?= e($brand['line2']) ?></b><?php endif; ?><small><?= e($brand['place']) ?></small></span></a>
    <nav><?php foreach ([
        '' => ['Dashboard','bi-grid'], 'balita' => ['Data Balita','bi-people'], 'pemeriksaan' => ['Pemeriksaan','bi-clipboard2-pulse'],
        'laporan' => ['Laporan','bi-file-earmark-bar-graph'], 'alat' => ['Alat IoT','bi-router'],
        'pengguna' => ['Pengguna','bi-person-gear']
    ] as $path => [$label,$icon]): if ($path === 'pengguna' && $user['role'] !== 'admin') continue; ?>
        <a class="<?= $active === $path ? 'active' : '' ?>" href="<?= e(url($path)) ?>"><i class="bi <?= e($icon) ?>"></i><?= e($label) ?></a>
    <?php endforeach; ?></nav>
    <div class="sidebar-user"><span><?= e($user['name']) ?></span><small><?= e(ucfirst($user['role'])) ?></small><a href="<?= e(url('logout')) ?>">Keluar</a></div>
</aside>
<main class="main"><header><button class="menu-toggle" type="button">☰</button><div><h1><?= e($title) ?></h1><p><?= e(date('l, d F Y')) ?></p></div></header>
<?php else: ?><main class="auth-main">
<?php endif; ?>
<?php foreach ($flashes as $message): ?><div class="alert alert-<?= e($message['type']) ?> alert-dismissible fade show" role="alert"><?= e($message['message']) ?><button type="button" class="btn-close" data-bs-dismiss="alert"></button></div><?php endforeach; ?>
<?php $content(); ?>
</main><?php if ($user): ?></div><?php endif; ?>
<script src="<?= e(url('assets/vendor/bootstrap.bundle.min.js')) ?>"></script>
<script src="<?= e(url('assets/vendor/chart.umd.min.js')) ?>"></script>
<script src="<?= e(asset_url('assets/app.js')) ?>"></script>
</body></html><?php
}

function json_response(array $data, int $status = 200): never
{
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store');
    echo json_encode($data, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}
