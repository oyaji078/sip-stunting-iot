<?php

declare(strict_types=1);

$root = dirname(__DIR__);
$configFile = $root . '/config/local.php';
if (!is_file($configFile)) {
    http_response_code(503);
    exit('Konfigurasi aplikasi belum tersedia. Salin config/config.example.php menjadi config/local.php.');
}

$config = require $configFile;
date_default_timezone_set($config['app']['timezone'] ?? 'Asia/Makassar');

if (PHP_SAPI !== 'cli' && session_status() !== PHP_SESSION_ACTIVE) {
    session_name($config['app']['session_name'] ?? 'sip_stunting_session');
    session_set_cookie_params([
        'lifetime' => 0,
        'path' => '/',
        'secure' => (bool)($config['app']['secure_cookie'] ?? true),
        'httponly' => true,
        'samesite' => 'Lax',
    ]);
    session_start();
}

try {
    $pdo = new PDO(
        $config['db']['dsn'],
        $config['db']['user'],
        $config['db']['password'],
        [
            PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
            PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
            PDO::ATTR_EMULATE_PREPARES => false,
        ]
    );
} catch (PDOException $e) {
    error_log('Database connection failed: ' . $e->getMessage());
    http_response_code(503);
    exit('Database belum dapat dihubungi.');
}

require_once $root . '/app/functions.php';
require_once $root . '/app/GrowthService.php';
require_once $root . '/app/ApiController.php';

