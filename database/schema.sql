CREATE TABLE IF NOT EXISTS migrations (
    version VARCHAR(100) PRIMARY KEY,
    applied_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS users (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    username VARCHAR(80) NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,
    role ENUM('admin','petugas') NOT NULL DEFAULT 'petugas',
    is_active TINYINT(1) NOT NULL DEFAULT 1,
    must_change_password TINYINT(1) NOT NULL DEFAULT 1,
    failed_attempts SMALLINT UNSIGNED NOT NULL DEFAULT 0,
    locked_until DATETIME NULL,
    last_login_at DATETIME NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS children (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    rfid_uid VARCHAR(32) NOT NULL UNIQUE,
    nik VARCHAR(30) NULL UNIQUE,
    name VARCHAR(120) NOT NULL,
    birth_date DATE NOT NULL,
    sex ENUM('L','P') NOT NULL,
    parent_name VARCHAR(120) NULL,
    phone VARCHAR(30) NULL,
    address TEXT NULL,
    is_active TINYINT(1) NOT NULL DEFAULT 1,
    created_by BIGINT UNSIGNED NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    CONSTRAINT fk_children_user FOREIGN KEY (created_by) REFERENCES users(id) ON DELETE SET NULL,
    INDEX idx_children_name (name), INDEX idx_children_birth (birth_date)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS devices (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    device_uid VARCHAR(40) NOT NULL UNIQUE,
    name VARCHAR(100) NOT NULL,
    token_hash CHAR(64) NOT NULL,
    firmware_version VARCHAR(40) NULL,
    wifi_rssi SMALLINT NULL,
    last_ip VARCHAR(45) NULL,
    last_seen_at DATETIME NULL,
    is_active TINYINT(1) NOT NULL DEFAULT 1,
    created_by BIGINT UNSIGNED NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    CONSTRAINT fk_devices_user FOREIGN KEY (created_by) REFERENCES users(id) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS unknown_rfid_scans (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    rfid_uid VARCHAR(32) NOT NULL,
    device_id BIGINT UNSIGNED NOT NULL,
    scan_count INT UNSIGNED NOT NULL DEFAULT 1,
    first_scanned_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_scanned_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    resolved_child_id BIGINT UNSIGNED NULL,
    resolved_at DATETIME NULL,
    CONSTRAINT fk_scans_device FOREIGN KEY (device_id) REFERENCES devices(id),
    CONSTRAINT fk_scans_child FOREIGN KEY (resolved_child_id) REFERENCES children(id) ON DELETE SET NULL,
    UNIQUE KEY uq_unknown_scan (rfid_uid, device_id), INDEX idx_unknown_last (last_scanned_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS who_lms (
    sex TINYINT UNSIGNED NOT NULL,
    age_days SMALLINT UNSIGNED NOT NULL,
    l DECIMAL(12,8) NOT NULL,
    m DECIMAL(12,6) NOT NULL,
    s DECIMAL(12,8) NOT NULL,
    measurement_type CHAR(1) NOT NULL,
    PRIMARY KEY (sex, age_days)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS measurements (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    request_id VARCHAR(80) NULL,
    child_id BIGINT UNSIGNED NOT NULL,
    device_id BIGINT UNSIGNED NULL,
    recorded_by BIGINT UNSIGNED NULL,
    source ENUM('iot','manual') NOT NULL,
    status ENUM('pending','approved') NOT NULL DEFAULT 'approved',
    approved_by BIGINT UNSIGNED NULL,
    approved_at DATETIME NULL,
    measured_at DATETIME NOT NULL,
    age_days SMALLINT UNSIGNED NOT NULL,
    weight_kg DECIMAL(6,2) NOT NULL,
    height_cm DECIMAL(6,2) NOT NULL,
    z_score DECIMAL(7,4) NOT NULL,
    category ENUM('Sangat Pendek','Pendek/Stunting','Normal','Tinggi') NOT NULL,
    reference_version VARCHAR(80) NOT NULL,
    voided_at DATETIME NULL,
    voided_by BIGINT UNSIGNED NULL,
    void_reason VARCHAR(255) NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_measure_child FOREIGN KEY (child_id) REFERENCES children(id),
    CONSTRAINT fk_measure_device FOREIGN KEY (device_id) REFERENCES devices(id) ON DELETE SET NULL,
    CONSTRAINT fk_measure_user FOREIGN KEY (recorded_by) REFERENCES users(id) ON DELETE SET NULL,
    CONSTRAINT fk_measure_void_user FOREIGN KEY (voided_by) REFERENCES users(id) ON DELETE SET NULL,
    CONSTRAINT fk_measure_approved_user FOREIGN KEY (approved_by) REFERENCES users(id) ON DELETE SET NULL,
    UNIQUE KEY uq_device_request (device_id, request_id),
    INDEX idx_measure_date (measured_at), INDEX idx_measure_category (category), INDEX idx_measure_child (child_id), INDEX idx_measure_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS settings (
    setting_key VARCHAR(80) PRIMARY KEY,
    setting_value TEXT NULL,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS audit_logs (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id BIGINT UNSIGNED NULL,
    action VARCHAR(100) NOT NULL,
    entity_type VARCHAR(60) NULL,
    entity_id BIGINT UNSIGNED NULL,
    details_json JSON NULL,
    ip_address VARCHAR(45) NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_audit_user FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE SET NULL,
    INDEX idx_audit_created (created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT INTO settings (setting_key, setting_value) VALUES
('app_name', 'SIP Stunting IoT'), ('reference_version', 'WHO-CGS-2006-lenanthro-daily')
ON DUPLICATE KEY UPDATE setting_value = VALUES(setting_value);

