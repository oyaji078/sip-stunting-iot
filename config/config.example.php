<?php

declare(strict_types=1);

return [
    'app' => [
        'name' => 'SIP Stunting IoT',
        'url' => 'https://posyandu.oypoint.online',
        'timezone' => 'Asia/Makassar',
        'setup_token' => 'GANTI_DENGAN_TOKEN_ACAK_MINIMAL_32_KARAKTER',
        'session_name' => 'sip_stunting_session',
        'secure_cookie' => true,
    ],
    'db' => [
        'dsn' => 'mysql:host=127.0.0.1;port=3306;dbname=NAMA_DATABASE;charset=utf8mb4',
        'user' => 'USER_DATABASE',
        'password' => 'PASSWORD_DATABASE',
    ],
];

