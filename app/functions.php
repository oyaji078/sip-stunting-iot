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

function redirect(string $path): never
{
    header('Location: ' . ($path === '' || str_starts_with($path, 'http') ? $path : url($path)));
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
        exit('Sesi formulir kedaluwarsa. Muat ulang halaman dan coba lagi.');
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
        exit('Anda tidak memiliki hak akses untuk tindakan ini.');
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
        throw new InvalidArgumentException('UID RFID tidak valid.');
    }
    return implode(':', str_split($hex, 2));
}

function device_status(?string $lastSeen): array
{
    if (!$lastSeen) {
        return ['Belum aktif', 'secondary'];
    }
    $online = time() - strtotime($lastSeen) <= 90;
    return $online ? ['Online', 'success'] : ['Offline', 'danger'];
}

function render(string $title, callable $content, string $active = ''): void
{
    global $config;
    $user = current_user();
    $flashes = pull_flashes();
    ?><!doctype html>
<html lang="id"><head>
    <meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
    <title><?= e($title) ?> · <?= e($config['app']['name']) ?></title>
    <link rel="stylesheet" href="<?= e(url('assets/vendor/bootstrap.min.css')) ?>">
    <link rel="stylesheet" href="<?= e(url('assets/app.css')) ?>">
</head><body>
<?php if ($user): ?>
<div class="app-shell">
<aside class="sidebar">
    <a class="brand" href="<?= e(url()) ?>"><span class="brand-mark">S</span><span><b>SIP Stunting</b><small>IoT Posyandu</small></span></a>
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
<script src="<?= e(url('assets/app.js')) ?>"></script>
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
