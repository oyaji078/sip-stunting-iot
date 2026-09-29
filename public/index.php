<?php

declare(strict_types=1);

require dirname(__DIR__) . '/app/bootstrap.php';

$path = trim((string)parse_url($_SERVER['REQUEST_URI'] ?? '/', PHP_URL_PATH), '/');
$basePath = trim((string)parse_url($config['app']['url'], PHP_URL_PATH), '/');
if ($basePath && str_starts_with($path, $basePath)) $path = trim(substr($path, strlen($basePath)), '/');
$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';

if (str_starts_with($path, 'api/v1/')) {
    (new ApiController($pdo))->dispatch(substr($path, 7));
}

if ($path === 'setup') {
    if ($method === 'POST') {
        verify_csrf();
        if (!hash_equals((string)$config['app']['setup_token'], (string)($_POST['setup_token'] ?? ''))) {
            flash('danger', 'Token setup tidak benar.'); redirect('setup');
        }
        try {
            $sql = file_get_contents(dirname(__DIR__) . '/database/schema.sql');
            foreach (preg_split('/;\s*(?:\r?\n|$)/', $sql) as $statement) {
                if (trim($statement) !== '') $pdo->exec($statement);
            }
            $count = (int)$pdo->query('SELECT COUNT(*) FROM who_lms')->fetchColumn();
            if ($count < 3000) {
                $pdo->beginTransaction();
                $insert = $pdo->prepare('INSERT IGNORE INTO who_lms (sex, age_days, l, m, s, measurement_type) VALUES (?, ?, ?, ?, ?, ?)');
                $file = new SplFileObject(dirname(__DIR__) . '/database/who_lenanthro.tsv');
                $file->fgets();
                while (!$file->eof()) {
                    $row = str_getcsv(trim($file->fgets()), "\t");
                    if (count($row) === 6) $insert->execute($row);
                }
                $pdo->commit();
            }
            $username = 'admin';
            $password = rtrim(strtr(base64_encode(random_bytes(12)), '+/', '-_'), '=');
            $stmt = $pdo->prepare('INSERT INTO users (name, username, password_hash, role, must_change_password) VALUES (?, ?, ?, \'admin\', 1) ON DUPLICATE KEY UPDATE username = username');
            $stmt->execute(['Administrator', $username, password_hash($password, PASSWORD_DEFAULT)]);
            $_SESSION['setup_credentials'] = ['username' => $username, 'password' => $password];
            redirect('setup');
        } catch (Throwable $e) {
            if ($pdo->inTransaction()) $pdo->rollBack();
            error_log($e->getMessage());
            flash('danger', 'Setup gagal. Periksa koneksi database dan log server.'); redirect('setup');
        }
    }
    $credentials = $_SESSION['setup_credentials'] ?? null;
    unset($_SESSION['setup_credentials']);
    render('Setup Sistem', function () use ($credentials) { ?>
    <div class="auth-card card"><div class="card-body p-4 p-md-5"><div class="text-center mb-4"><div class="brand-mark mx-auto mb-3">S</div><h2>Setup SIP Stunting IoT</h2></div>
    <?php if ($credentials): ?><div class="alert alert-success"><b>Setup selesai.</b><br>Username: <code><?= e($credentials['username']) ?></code><br>Password sementara: <code><?= e($credentials['password']) ?></code><br><small>Simpan sekarang; password ini hanya ditampilkan sekali.</small></div><a class="btn btn-primary w-100" href="<?= e(url('login')) ?>">Masuk ke aplikasi</a>
    <?php else: ?><form method="post"><?= csrf_field() ?><label class="form-label">Token setup</label><input class="form-control mb-3" type="password" name="setup_token" required autocomplete="off"><button class="btn btn-primary w-100">Jalankan migrasi dan buat admin</button></form><?php endif; ?>
    </div></div><?php }, ''); exit;
}

if ($path === 'login') {
    if (current_user()) redirect('');
    if ($method === 'POST') {
        verify_csrf();
        $username = strtolower(trim((string)($_POST['username'] ?? '')));
        $stmt = $pdo->prepare('SELECT * FROM users WHERE username = ? AND is_active = 1 LIMIT 1');
        $stmt->execute([$username]); $user = $stmt->fetch();
        if ($user && $user['locked_until'] && strtotime($user['locked_until']) > time()) {
            flash('danger', 'Akun dikunci sementara. Coba kembali beberapa menit lagi.');
        } elseif ($user && password_verify((string)($_POST['password'] ?? ''), $user['password_hash'])) {
            session_regenerate_id(true); $_SESSION['user_id'] = $user['id'];
            $pdo->prepare('UPDATE users SET failed_attempts = 0, locked_until = NULL, last_login_at = NOW() WHERE id = ?')->execute([$user['id']]);
            audit('login'); redirect($user['must_change_password'] ? 'ubah-password' : '');
        } else {
            if ($user) {
                $attempts = (int)$user['failed_attempts'] + 1;
                $locked = $attempts >= 5 ? date('Y-m-d H:i:s', time() + 900) : null;
                $pdo->prepare('UPDATE users SET failed_attempts = ?, locked_until = ? WHERE id = ?')->execute([$attempts, $locked, $user['id']]);
            }
            usleep(300000); flash('danger', 'Username atau password salah.');
        }
        redirect('login');
    }
    render('Masuk', function () { ?><div class="auth-card card"><div class="card-body p-4 p-md-5"><div class="text-center mb-4"><div class="brand-mark mx-auto mb-3">S</div><h2>SIP Stunting IoT</h2><p class="text-muted">Masuk untuk mengelola layanan Posyandu</p></div><form method="post"><?= csrf_field() ?><label class="form-label">Username</label><input class="form-control mb-3" name="username" required autofocus><label class="form-label">Password</label><input class="form-control mb-4" type="password" name="password" required><button class="btn btn-primary w-100">Masuk</button></form></div></div><?php }, ''); exit;
}

if ($path === 'logout') {
    if (current_user()) audit('logout');
    $_SESSION = []; if (session_status() === PHP_SESSION_ACTIVE) session_destroy(); redirect('login');
}

$user = require_auth();
if ($user['must_change_password'] && $path !== 'ubah-password') redirect('ubah-password');

if ($path === 'ubah-password') {
    if ($method === 'POST') {
        verify_csrf(); $password = (string)($_POST['password'] ?? '');
        if (strlen($password) < 10 || !preg_match('/[A-Z]/', $password) || !preg_match('/[a-z]/', $password) || !preg_match('/\d/', $password)) {
            flash('danger', 'Password minimal 10 karakter dan memuat huruf besar, huruf kecil, serta angka.');
        } elseif (!hash_equals($password, (string)($_POST['password_confirmation'] ?? ''))) {
            flash('danger', 'Konfirmasi password tidak sama.');
        } else {
            $pdo->prepare('UPDATE users SET password_hash = ?, must_change_password = 0 WHERE id = ?')->execute([password_hash($password, PASSWORD_DEFAULT), $user['id']]);
            audit('change_password', 'user', (int)$user['id']); flash('success', 'Password berhasil diperbarui.'); redirect('');
        }
        redirect('ubah-password');
    }
    render('Ganti Password', function () { ?><div class="card panel"><div class="card-body"><p>Password sementara wajib diganti sebelum melanjutkan.</p><form method="post" class="form-narrow"><?= csrf_field() ?><label class="form-label">Password baru</label><input class="form-control mb-3" type="password" name="password" required><label class="form-label">Ulangi password</label><input class="form-control mb-3" type="password" name="password_confirmation" required><button class="btn btn-primary">Simpan Password</button></form></div></div><?php }); exit;
}

if ($path === '') {
    $stats = [
        'children' => (int)$pdo->query('SELECT COUNT(*) FROM children WHERE is_active = 1')->fetchColumn(),
        'today' => (int)$pdo->query('SELECT COUNT(*) FROM measurements WHERE DATE(measured_at) = CURDATE() AND voided_at IS NULL')->fetchColumn(),
        'stunting' => (int)$pdo->query("SELECT COUNT(*) FROM measurements WHERE category IN ('Sangat Pendek','Pendek/Stunting') AND voided_at IS NULL AND measured_at >= DATE_SUB(NOW(), INTERVAL 30 DAY)")->fetchColumn(),
        'online' => (int)$pdo->query('SELECT COUNT(*) FROM devices WHERE is_active = 1 AND last_seen_at >= DATE_SUB(NOW(), INTERVAL 90 SECOND)')->fetchColumn(),
    ];
    $recent = $pdo->query('SELECT m.*, c.name child_name FROM measurements m JOIN children c ON c.id=m.child_id WHERE m.voided_at IS NULL ORDER BY m.measured_at DESC LIMIT 8')->fetchAll();
    $categories = $pdo->query('SELECT category, COUNT(*) total FROM measurements WHERE voided_at IS NULL GROUP BY category')->fetchAll();
    $attention = $pdo->query("SELECT c.name, m.category, m.z_score, m.measured_at FROM measurements m JOIN children c ON c.id=m.child_id JOIN (SELECT child_id, MAX(measured_at) latest FROM measurements WHERE voided_at IS NULL GROUP BY child_id) x ON x.child_id=m.child_id AND x.latest=m.measured_at WHERE m.category IN ('Sangat Pendek','Pendek/Stunting') ORDER BY m.z_score ASC LIMIT 6")->fetchAll();
    render('Dashboard', function () use ($stats,$recent,$categories,$attention) { ?>
    <div class="stats-grid"><?php foreach ([['Balita aktif',$stats['children'],'primary'],['Pemeriksaan hari ini',$stats['today'],'info'],['Perlu perhatian (30 hari)',$stats['stunting'],'warning'],['Alat online',$stats['online'],'success']] as [$label,$value,$color]): ?><div class="stat-card"><span class="dot bg-<?= e($color) ?>"></span><div><small><?= e($label) ?></small><strong><?= e($value) ?></strong></div></div><?php endforeach; ?></div>
    <div class="row g-4 mt-1"><div class="col-lg-8"><div class="card panel h-100"><div class="card-header"><h2>Pemeriksaan terbaru</h2><a href="<?= e(url('pemeriksaan')) ?>">Lihat semua</a></div><div class="table-responsive"><table class="table align-middle"><thead><tr><th>Balita</th><th>Waktu</th><th>Tinggi</th><th>Z-score</th><th>Status</th></tr></thead><tbody><?php foreach($recent as $r): ?><tr><td><b><?= e($r['child_name']) ?></b></td><td><?= e(date('d/m/Y H:i',strtotime($r['measured_at']))) ?></td><td><?= e($r['height_cm']) ?> cm</td><td><?= e(number_format((float)$r['z_score'],2)) ?></td><td><span class="status status-<?= str_contains($r['category'],'Pendek') ? 'danger':'success' ?>"><?= e($r['category']) ?></span></td></tr><?php endforeach; ?><?php if(!$recent): ?><tr><td colspan="5" class="empty">Belum ada pemeriksaan.</td></tr><?php endif; ?></tbody></table></div></div></div>
    <div class="col-lg-4"><div class="card panel mb-4"><div class="card-header"><h2>Distribusi status</h2></div><div class="card-body"><canvas id="categoryChart" data-values='<?= e(json_encode($categories)) ?>'></canvas></div></div><div class="card panel"><div class="card-header"><h2>Perlu perhatian</h2></div><div class="list-group list-group-flush"><?php foreach($attention as $a): ?><div class="list-group-item d-flex justify-content-between"><span><b><?= e($a['name']) ?></b><small class="d-block text-muted"><?= e($a['category']) ?></small></span><b><?= e(number_format((float)$a['z_score'],2)) ?></b></div><?php endforeach; ?><?php if(!$attention): ?><div class="empty">Tidak ada data.</div><?php endif; ?></div></div></div></div>
    <?php }, ''); exit;
}

if ($path === 'balita/simpan' && $method === 'POST') {
    verify_csrf();
    try {
        $id = (int)($_POST['id'] ?? 0); $uid = normalize_uid((string)$_POST['rfid_uid']);
        $values = [trim((string)$_POST['name']), $_POST['birth_date'], $_POST['sex'], trim((string)$_POST['nik']) ?: null, trim((string)$_POST['parent_name']) ?: null, trim((string)$_POST['phone']) ?: null, trim((string)$_POST['address']) ?: null, isset($_POST['is_active']) ? 1 : 0];
        if ($values[0] === '' || !in_array($values[2], ['L','P'], true)) throw new InvalidArgumentException('Data wajib belum lengkap.');
        if ($id) {
            $stmt=$pdo->prepare('UPDATE children SET rfid_uid=?, name=?, birth_date=?, sex=?, nik=?, parent_name=?, phone=?, address=?, is_active=? WHERE id=?'); $stmt->execute([$uid,...$values,$id]);
            audit('update_child','child',$id); $childId=$id;
        } else {
            $stmt=$pdo->prepare('INSERT INTO children (rfid_uid,name,birth_date,sex,nik,parent_name,phone,address,is_active,created_by) VALUES (?,?,?,?,?,?,?,?,?,?)'); $stmt->execute([$uid,...$values,$user['id']]);
            $childId=(int)$pdo->lastInsertId(); audit('create_child','child',$childId);
        }
        $pdo->prepare('UPDATE unknown_rfid_scans SET resolved_child_id=?, resolved_at=NOW() WHERE rfid_uid=? AND resolved_at IS NULL')->execute([$childId,$uid]);
        flash('success','Data balita berhasil disimpan.'); redirect('balita');
    } catch (Throwable $e) { flash('danger', str_contains($e->getMessage(),'Duplicate') ? 'UID RFID atau NIK sudah digunakan.' : $e->getMessage()); redirect('balita'); }
}

if ($path === 'balita') {
    $edit=null; if(isset($_GET['edit'])) { $s=$pdo->prepare('SELECT * FROM children WHERE id=?');$s->execute([(int)$_GET['edit']]);$edit=$s->fetch(); }
    $q=trim((string)($_GET['q']??''));$s=$pdo->prepare('SELECT c.*, (SELECT MAX(measured_at) FROM measurements m WHERE m.child_id=c.id AND m.voided_at IS NULL) last_measurement FROM children c WHERE c.name LIKE ? OR c.rfid_uid LIKE ? ORDER BY c.name');$s->execute(["%$q%","%$q%"]);$children=$s->fetchAll();
    $unknown=$pdo->query('SELECT s.*,d.name device_name FROM unknown_rfid_scans s JOIN devices d ON d.id=s.device_id WHERE s.resolved_at IS NULL ORDER BY s.last_scanned_at DESC LIMIT 10')->fetchAll();
    render('Data Balita', function() use($children,$unknown,$edit,$q){ ?>
    <div class="row g-4"><div class="col-xl-8"><div class="card panel"><div class="card-header flex-wrap"><h2>Daftar balita</h2><form class="d-flex"><input class="form-control" name="q" value="<?= e($q) ?>" placeholder="Cari nama atau RFID"><button class="btn btn-outline-primary ms-2">Cari</button></form></div><div class="table-responsive"><table class="table align-middle"><thead><tr><th>Balita</th><th>RFID</th><th>L/P</th><th>Lahir</th><th>Status</th><th></th></tr></thead><tbody><?php foreach($children as $c): ?><tr><td><b><?= e($c['name']) ?></b><small class="d-block text-muted"><?= e($c['parent_name']) ?></small></td><td><code><?= e($c['rfid_uid']) ?></code></td><td><?= e($c['sex']) ?></td><td><?= e(date('d/m/Y',strtotime($c['birth_date']))) ?></td><td><span class="status status-<?= $c['is_active']?'success':'secondary' ?>"><?= $c['is_active']?'Aktif':'Nonaktif' ?></span></td><td><a class="btn btn-sm btn-light" href="?edit=<?= e($c['id']) ?>">Edit</a></td></tr><?php endforeach; ?><?php if(!$children): ?><tr><td colspan="6" class="empty">Data tidak ditemukan.</td></tr><?php endif; ?></tbody></table></div></div>
    <div class="card panel mt-4"><div class="card-header"><h2>RFID belum terdaftar</h2></div><div class="table-responsive"><table class="table"><thead><tr><th>UID</th><th>Alat</th><th>Terakhir</th><th></th></tr></thead><tbody><?php foreach($unknown as $s): ?><tr><td><code><?= e($s['rfid_uid']) ?></code></td><td><?= e($s['device_name']) ?></td><td><?= e(date('d/m/Y H:i',strtotime($s['last_scanned_at']))) ?></td><td><a href="?uid=<?= urlencode($s['rfid_uid']) ?>" class="btn btn-sm btn-primary">Daftarkan</a></td></tr><?php endforeach; ?><?php if(!$unknown): ?><tr><td colspan="4" class="empty">Tidak ada antrean RFID.</td></tr><?php endif; ?></tbody></table></div></div></div>
    <div class="col-xl-4"><div class="card panel sticky-xl-top"><div class="card-header"><h2><?= $edit?'Edit':'Tambah' ?> balita</h2></div><div class="card-body"><form method="post" action="<?= e(url('balita/simpan')) ?>"><?= csrf_field() ?><input type="hidden" name="id" value="<?= e($edit['id']??'') ?>"><label class="form-label">UID RFID</label><input class="form-control mb-3" name="rfid_uid" value="<?= e($edit['rfid_uid']??($_GET['uid']??'')) ?>" required><label class="form-label">Nama balita</label><input class="form-control mb-3" name="name" value="<?= e($edit['name']??'') ?>" required><div class="row"><div class="col"><label class="form-label">Tanggal lahir</label><input class="form-control mb-3" type="date" name="birth_date" value="<?= e($edit['birth_date']??'') ?>" required></div><div class="col"><label class="form-label">Jenis kelamin</label><select class="form-select" name="sex"><option value="L" <?= ($edit['sex']??'')==='L'?'selected':'' ?>>Laki-laki</option><option value="P" <?= ($edit['sex']??'')==='P'?'selected':'' ?>>Perempuan</option></select></div></div><label class="form-label">NIK (opsional)</label><input class="form-control mb-3" name="nik" value="<?= e($edit['nik']??'') ?>"><label class="form-label">Orang tua/wali</label><input class="form-control mb-3" name="parent_name" value="<?= e($edit['parent_name']??'') ?>"><label class="form-label">Telepon</label><input class="form-control mb-3" name="phone" value="<?= e($edit['phone']??'') ?>"><label class="form-label">Alamat</label><textarea class="form-control mb-3" name="address"><?= e($edit['address']??'') ?></textarea><label class="form-check mb-3"><input class="form-check-input" type="checkbox" name="is_active" <?= !isset($edit)||$edit['is_active']?'checked':'' ?>> <span class="form-check-label">Data aktif</span></label><button class="btn btn-primary w-100">Simpan</button></form></div></div></div></div><?php },'balita');exit;
}

if ($path === 'pemeriksaan/manual' && $method === 'POST') {
    verify_csrf();
    try { $childId=(int)$_POST['child_id'];$s=$pdo->prepare('SELECT * FROM children WHERE id=? AND is_active=1');$s->execute([$childId]);$child=$s->fetch();if(!$child)throw new DomainException('Balita tidak ditemukan.');$date=str_replace('T',' ',(string)$_POST['measured_at']);$weight=(float)$_POST['weight_kg'];$height=(float)$_POST['height_cm'];if($weight<2||$weight>100)throw new DomainException('Berat harus 2–100 kg.');$growth=(new GrowthService($pdo))->calculate($child['sex'],$child['birth_date'],substr($date,0,10),$height);$i=$pdo->prepare("INSERT INTO measurements (child_id,recorded_by,source,measured_at,age_days,weight_kg,height_cm,z_score,category,reference_version) VALUES (?,?,'manual',?,?,?,?,?,?,?)");$i->execute([$childId,$user['id'],$date,$growth['age_days'],$weight,$height,$growth['z_score'],$growth['category'],$growth['reference_version']]);audit('create_manual_measurement','measurement',(int)$pdo->lastInsertId());flash('success','Pemeriksaan manual berhasil disimpan.');
    } catch(Throwable $e){flash('danger',$e->getMessage());}redirect('pemeriksaan');
}

if ($path === 'pemeriksaan/batalkan' && $method === 'POST') {
    require_auth('admin');verify_csrf();$id=(int)$_POST['id'];$reason=trim((string)$_POST['reason']);if(strlen($reason)<5){flash('danger','Alasan pembatalan minimal 5 karakter.');redirect('pemeriksaan');}$pdo->prepare('UPDATE measurements SET voided_at=NOW(),voided_by=?,void_reason=? WHERE id=? AND voided_at IS NULL')->execute([$user['id'],$reason,$id]);audit('void_measurement','measurement',$id,['reason'=>$reason]);flash('success','Pemeriksaan dibatalkan dan tetap tersimpan untuk audit.');redirect('pemeriksaan');
}

if ($path === 'pemeriksaan') {
    $children=$pdo->query('SELECT id,name FROM children WHERE is_active=1 ORDER BY name')->fetchAll();$rows=$pdo->query('SELECT m.*,c.name child_name,d.name device_name,u.name officer_name FROM measurements m JOIN children c ON c.id=m.child_id LEFT JOIN devices d ON d.id=m.device_id LEFT JOIN users u ON u.id=m.recorded_by ORDER BY m.measured_at DESC LIMIT 100')->fetchAll();
    render('Pemeriksaan',function()use($children,$rows,$user){?><div class="row g-4"><div class="col-xl-8"><div class="card panel"><div class="card-header"><h2>Riwayat pemeriksaan</h2><a class="btn btn-outline-primary btn-sm" href="<?= e(url('laporan')) ?>">Filter lengkap</a></div><div class="table-responsive"><table class="table align-middle"><thead><tr><th>Balita/Waktu</th><th>Sumber</th><th>BB/TB</th><th>Z-score</th><th>Status</th><th></th></tr></thead><tbody><?php foreach($rows as$r):?><tr class="<?= $r['voided_at']?'voided':'' ?>"><td><b><?=e($r['child_name'])?></b><small class="d-block text-muted"><?=e(date('d/m/Y H:i',strtotime($r['measured_at'])))?></small></td><td><?=e(strtoupper($r['source']))?><small class="d-block text-muted"><?=e($r['device_name']??$r['officer_name'])?></small></td><td><?=e($r['weight_kg'])?> kg<br><?=e($r['height_cm'])?> cm</td><td><?=e(number_format((float)$r['z_score'],2))?></td><td><span class="status status-<?=str_contains($r['category'],'Pendek')?'danger':'success'?>"><?=e($r['voided_at']?'Dibatalkan':$r['category'])?></span><?php if($r['void_reason']):?><small class="d-block"><?=e($r['void_reason'])?></small><?php endif;?></td><td><?php if($user['role']==='admin'&&!$r['voided_at']):?><button class="btn btn-sm btn-light" data-bs-toggle="modal" data-bs-target="#voidModal" data-id="<?=e($r['id'])?>">Batalkan</button><?php endif;?></td></tr><?php endforeach;?></tbody></table></div></div></div><div class="col-xl-4"><div class="card panel"><div class="card-header"><h2>Input manual</h2></div><div class="card-body"><form method="post" action="<?=e(url('pemeriksaan/manual'))?>"><?=csrf_field()?><label class="form-label">Balita</label><select class="form-select mb-3" name="child_id" required><option value="">Pilih balita</option><?php foreach($children as$c):?><option value="<?=e($c['id'])?>"><?=e($c['name'])?></option><?php endforeach;?></select><label class="form-label">Waktu ukur</label><input class="form-control mb-3" type="datetime-local" name="measured_at" value="<?=e(date('Y-m-d\TH:i'))?>" required><div class="row"><div class="col"><label class="form-label">Berat (kg)</label><input class="form-control" type="number" step="0.01" min="2" max="100" name="weight_kg" required></div><div class="col"><label class="form-label">Tinggi (cm)</label><input class="form-control" type="number" step="0.01" min="50" max="150" name="height_cm" required></div></div><button class="btn btn-primary w-100 mt-4">Hitung dan simpan</button></form></div></div></div></div>
    <div class="modal fade" id="voidModal" tabindex="-1"><div class="modal-dialog"><form class="modal-content" method="post" action="<?=e(url('pemeriksaan/batalkan'))?>"><?=csrf_field()?><input type="hidden" name="id"><div class="modal-header"><h5 class="modal-title">Batalkan pemeriksaan</h5><button type="button" class="btn-close" data-bs-dismiss="modal"></button></div><div class="modal-body"><label class="form-label">Alasan</label><textarea class="form-control" name="reason" required minlength="5"></textarea></div><div class="modal-footer"><button class="btn btn-danger">Batalkan pemeriksaan</button></div></form></div></div><?php },'pemeriksaan');exit;
}

function report_query(PDO $pdo): array { $where=['m.voided_at IS NULL'];$params=[];foreach(['from'=>'m.measured_at >= ?','to'=>'m.measured_at < DATE_ADD(?, INTERVAL 1 DAY)','child_id'=>'m.child_id = ?','sex'=>'c.sex = ?','source'=>'m.source = ?','device_id'=>'m.device_id = ?','category'=>'m.category = ?'] as$key=>$clause){if(isset($_GET[$key])&&$_GET[$key]!==''){$where[]=$clause;$params[]=$_GET[$key];}}$s=$pdo->prepare('SELECT m.*,c.name child_name,c.sex,d.name device_name,u.name officer_name FROM measurements m JOIN children c ON c.id=m.child_id LEFT JOIN devices d ON d.id=m.device_id LEFT JOIN users u ON u.id=m.recorded_by WHERE '.implode(' AND ',$where).' ORDER BY m.measured_at DESC');$s->execute($params);return$s->fetchAll();}
if($path==='laporan/csv'){ $rows=report_query($pdo);header('Content-Type: text/csv; charset=UTF-8');header('Content-Disposition: attachment; filename="laporan-stunting-'.date('Ymd-His').'.csv"');echo "\xEF\xBB\xBF";$o=fopen('php://output','w');fputcsv($o,['Tanggal','Balita','JK','Sumber','Alat/Petugas','Berat (kg)','Tinggi (cm)','Umur (hari)','Z-score','Kategori']);foreach($rows as$r)fputcsv($o,[$r['measured_at'],$r['child_name'],$r['sex'],$r['source'],$r['device_name']??$r['officer_name'],$r['weight_kg'],$r['height_cm'],$r['age_days'],$r['z_score'],$r['category']]);fclose($o);exit;}
if($path==='laporan'){ $rows=report_query($pdo);$children=$pdo->query('SELECT id,name FROM children ORDER BY name')->fetchAll();$devices=$pdo->query('SELECT id,name FROM devices ORDER BY name')->fetchAll();render('Laporan',function()use($rows,$children,$devices){?><div class="card panel no-print"><div class="card-body"><form class="row g-3"><div class="col-md-2"><label class="form-label">Dari</label><input class="form-control" type="date" name="from" value="<?=e($_GET['from']??'')?>"></div><div class="col-md-2"><label class="form-label">Sampai</label><input class="form-control" type="date" name="to" value="<?=e($_GET['to']??'')?>"></div><div class="col-md-2"><label class="form-label">Balita</label><select class="form-select" name="child_id"><option value="">Semua</option><?php foreach($children as$c):?><option value="<?=e($c['id'])?>" <?=($_GET['child_id']??'')==$c['id']?'selected':''?>><?=e($c['name'])?></option><?php endforeach;?></select></div><div class="col-md-2"><label class="form-label">Sumber</label><select class="form-select" name="source"><option value="">Semua</option><option value="iot" <?=($_GET['source']??'')==='iot'?'selected':''?>>IoT</option><option value="manual" <?=($_GET['source']??'')==='manual'?'selected':''?>>Manual</option></select></div><div class="col-md-2"><label class="form-label">Kategori</label><select class="form-select" name="category"><option value="">Semua</option><?php foreach(['Sangat Pendek','Pendek/Stunting','Normal','Tinggi']as$c):?><option <?=($_GET['category']??'')===$c?'selected':''?>><?=e($c)?></option><?php endforeach;?></select></div><div class="col-md-2 d-flex align-items-end"><button class="btn btn-primary w-100">Terapkan</button></div></form></div></div><div class="d-flex justify-content-between align-items-center my-4"><div><h2 class="mb-0">Laporan Pemeriksaan</h2><small><?=e(count($rows))?> hasil</small></div><div class="no-print"><button class="btn btn-outline-primary" onclick="print()">Cetak / PDF</button> <a class="btn btn-primary" href="<?=e(url('laporan/csv').'?'.http_build_query($_GET))?>">Ekspor CSV</a></div></div><div class="card panel"><div class="table-responsive"><table class="table"><thead><tr><th>Tanggal</th><th>Balita</th><th>Sumber</th><th>Berat</th><th>Tinggi</th><th>Z-score</th><th>Kategori</th></tr></thead><tbody><?php foreach($rows as$r):?><tr><td><?=e(date('d/m/Y H:i',strtotime($r['measured_at'])))?></td><td><?=e($r['child_name'])?> (<?=e($r['sex'])?>)</td><td><?=e(strtoupper($r['source']))?></td><td><?=e($r['weight_kg'])?> kg</td><td><?=e($r['height_cm'])?> cm</td><td><?=e(number_format((float)$r['z_score'],2))?></td><td><?=e($r['category'])?></td></tr><?php endforeach;?></tbody></table></div></div><?php },'laporan');exit;}

if($path==='alat/simpan'&&$method==='POST'){require_auth('admin');verify_csrf();try{$uid=strtoupper(trim((string)$_POST['device_uid']));if(!preg_match('/^ESP32-[A-F0-9]{8,16}$/',$uid))throw new InvalidArgumentException('ID perangkat harus seperti ESP32-A1B2C3D4.');$token=bin2hex(random_bytes(24));$s=$pdo->prepare('INSERT INTO devices(device_uid,name,token_hash,created_by)VALUES(?,?,?,?)');$s->execute([$uid,trim((string)$_POST['name']),hash('sha256',$token),$user['id']]);audit('create_device','device',(int)$pdo->lastInsertId());$_SESSION['new_device_token']=['uid'=>$uid,'token'=>$token];flash('success','Perangkat ditambahkan. Salin token yang ditampilkan satu kali.');}catch(Throwable$e){flash('danger',str_contains($e->getMessage(),'Duplicate')?'ID perangkat sudah terdaftar.':$e->getMessage());}redirect('alat');}
if($path==='alat'){ $devices=$pdo->query('SELECT * FROM devices ORDER BY name')->fetchAll();$token=$_SESSION['new_device_token']??null;unset($_SESSION['new_device_token']);render('Alat IoT',function()use($devices,$token,$user){?><?php if($token):?><div class="alert alert-warning"><b>Token hanya ditampilkan sekali.</b><br>ID: <code><?=e($token['uid'])?></code><br>Token: <code class="select-all"><?=e($token['token'])?></code></div><?php endif;?><div class="row g-4"><div class="col-xl-8"><div class="row g-3"><?php foreach($devices as$d):[$label,$color]=device_status($d['last_seen_at']);?><div class="col-md-6"><div class="card device-card"><div class="card-body"><div class="d-flex justify-content-between"><div class="device-icon">⌁</div><span class="status status-<?=e($color)?>"><?=e($label)?></span></div><h3><?=e($d['name'])?></h3><code><?=e($d['device_uid'])?></code><hr><div class="device-meta"><span>Firmware<b><?=e($d['firmware_version']??'—')?></b></span><span>RSSI<b><?=e($d['wifi_rssi']!==null?$d['wifi_rssi'].' dBm':'—')?></b></span><span>Terakhir<b><?=e($d['last_seen_at']?date('d/m H:i:s',strtotime($d['last_seen_at'])):'Belum pernah')?></b></span></div></div></div></div><?php endforeach;?><?php if(!$devices):?><div class="empty">Belum ada perangkat.</div><?php endif;?></div></div><?php if($user['role']==='admin'):?><div class="col-xl-4"><div class="card panel"><div class="card-header"><h2>Daftarkan alat</h2></div><div class="card-body"><p class="text-muted">ID chip tampil pada LCD saat portal konfigurasi terbuka.</p><form method="post" action="<?=e(url('alat/simpan'))?>"><?=csrf_field()?><label class="form-label">Nama alat</label><input class="form-control mb-3" name="name" placeholder="Alat Posyandu 1" required><label class="form-label">ID perangkat</label><input class="form-control mb-3 text-uppercase" name="device_uid" placeholder="ESP32-A1B2C3D4" required><button class="btn btn-primary w-100">Buat token perangkat</button></form></div></div></div><?php endif;?></div><?php },'alat');exit;}

if($path==='pengguna/simpan'&&$method==='POST'){require_auth('admin');verify_csrf();try{$password=(string)$_POST['password'];if(strlen($password)<10)throw new InvalidArgumentException('Password minimal 10 karakter.');$s=$pdo->prepare('INSERT INTO users(name,username,password_hash,role,must_change_password)VALUES(?,?,?,?,1)');$s->execute([trim((string)$_POST['name']),strtolower(trim((string)$_POST['username'])),password_hash($password,PASSWORD_DEFAULT),$_POST['role']]);audit('create_user','user',(int)$pdo->lastInsertId());flash('success','Pengguna berhasil ditambahkan.');}catch(Throwable$e){flash('danger',str_contains($e->getMessage(),'Duplicate')?'Username sudah digunakan.':$e->getMessage());}redirect('pengguna');}
if($path==='pengguna'){require_auth('admin');$users=$pdo->query('SELECT * FROM users ORDER BY name')->fetchAll();render('Pengguna',function()use($users){?><div class="row g-4"><div class="col-xl-8"><div class="card panel"><div class="table-responsive"><table class="table"><thead><tr><th>Nama</th><th>Username</th><th>Peran</th><th>Status</th><th>Login terakhir</th></tr></thead><tbody><?php foreach($users as$u):?><tr><td><b><?=e($u['name'])?></b></td><td><?=e($u['username'])?></td><td><?=e(ucfirst($u['role']))?></td><td><?=e($u['is_active']?'Aktif':'Nonaktif')?></td><td><?=e($u['last_login_at']?date('d/m/Y H:i',strtotime($u['last_login_at'])):'—')?></td></tr><?php endforeach;?></tbody></table></div></div></div><div class="col-xl-4"><div class="card panel"><div class="card-header"><h2>Tambah pengguna</h2></div><div class="card-body"><form method="post" action="<?=e(url('pengguna/simpan'))?>"><?=csrf_field()?><label class="form-label">Nama</label><input class="form-control mb-3" name="name" required><label class="form-label">Username</label><input class="form-control mb-3" name="username" required><label class="form-label">Peran</label><select class="form-select mb-3" name="role"><option value="petugas">Petugas</option><option value="admin">Admin</option></select><label class="form-label">Password sementara</label><input class="form-control mb-3" type="password" name="password" minlength="10" required><button class="btn btn-primary w-100">Simpan</button></form></div></div></div></div><?php },'pengguna');exit;}

http_response_code(404);render('Halaman tidak ditemukan',fn()=>print('<div class="empty"><h2>404</h2><p>Halaman tidak ditemukan.</p></div>')); 
