<?php
// Passkeys (WebAuthn) without external libraries: just enough CBOR/COSE to
// read a public key at registration and verify signatures at sign-in.
// Supports ES256 (P-256, used by almost every passkey) and RS256 (Windows Hello).
declare(strict_types=1);

function b64url_encode(string $s): string
{
    return rtrim(strtr(base64_encode($s), '+/', '-_'), '=');
}

function b64url_decode(string $s): string
{
    $r = base64_decode(strtr($s, '-_', '+/') . str_repeat('=', (4 - strlen($s) % 4) % 4), true);
    if ($r === false) {
        throw new UserError('The passkey response was malformed.');
    }
    return $r;
}

/** Decodes the subset of CBOR that WebAuthn uses. */
final class Cbor
{
    private int $pos;

    private function __construct(private string $data, int $pos)
    {
        $this->pos = $pos;
    }

    /** Decodes one item starting at $offset and advances $offset past it. */
    public static function decode(string $data, int &$offset = 0)
    {
        $c = new self($data, $offset);
        $value = $c->item(0);
        $offset = $c->pos;
        return $value;
    }

    private function take(int $n): string
    {
        if ($n < 0 || $this->pos + $n > strlen($this->data)) {
            throw new UserError('The passkey response was malformed (CBOR).');
        }
        $s = substr($this->data, $this->pos, $n);
        $this->pos += $n;
        return $s;
    }

    private function length(int $info): int
    {
        if ($info < 24) {
            return $info;
        }
        switch ($info) {
            case 24: return ord($this->take(1));
            case 25: return unpack('n', $this->take(2))[1];
            case 26: return unpack('N', $this->take(4))[1];
            case 27: return unpack('J', $this->take(8))[1];
        }
        throw new UserError('Unsupported CBOR encoding.');
    }

    private function item(int $depth)
    {
        if ($depth > 16) {
            throw new UserError('CBOR nested too deeply.');
        }
        $b = ord($this->take(1));
        $major = $b >> 5;
        $info = $b & 31;
        if ($major === 7) {
            switch ($info) {
                case 20: return false;
                case 21: return true;
                case 22:
                case 23: return null;
                case 26: return unpack('G', $this->take(4))[1];
                case 27: return unpack('E', $this->take(8))[1];
            }
            throw new UserError('Unsupported CBOR value.');
        }
        $len = $this->length($info);
        switch ($major) {
            case 0: return $len;
            case 1: return -1 - $len;
            case 2:
            case 3: return $this->take($len);
            case 4:
                $out = [];
                for ($i = 0; $i < $len; $i++) {
                    $out[] = $this->item($depth + 1);
                }
                return $out;
            case 5:
                $out = [];
                for ($i = 0; $i < $len; $i++) {
                    $k = $this->item($depth + 1);
                    $out[$k] = $this->item($depth + 1);
                }
                return $out;
        }
        throw new UserError('Unsupported CBOR type.');
    }
}

function der_len(int $n): string
{
    if ($n < 128) {
        return chr($n);
    }
    $s = ltrim(pack('N', $n), "\0");
    return chr(0x80 | strlen($s)) . $s;
}

function der_seq(string $c): string
{
    return "\x30" . der_len(strlen($c)) . $c;
}

function der_int(string $bytes): string
{
    $bytes = ltrim($bytes, "\0");
    if ($bytes === '' || (ord($bytes[0]) & 0x80)) {
        $bytes = "\0" . $bytes;
    }
    return "\x02" . der_len(strlen($bytes)) . $bytes;
}

/** COSE public key → PEM (SubjectPublicKeyInfo). */
function cose_to_pem(array $k): string
{
    $kty = $k[1] ?? null;
    $alg = $k[3] ?? null;
    if ($kty === 2 && $alg === -7 && ($k[-1] ?? null) === 1 && strlen($k[-2] ?? '') === 32 && strlen($k[-3] ?? '') === 32) {
        $der = hex2bin('3059301306072a8648ce3d020106082a8648ce3d030107034200') . "\x04" . $k[-2] . $k[-3];
    } elseif ($kty === 3 && $alg === -257 && isset($k[-1], $k[-2])) {
        $rsa = der_seq(der_int($k[-1]) . der_int($k[-2]));
        $der = der_seq(der_seq(hex2bin('06092a864886f70d010101') . "\x05\x00") . "\x03" . der_len(strlen($rsa) + 1) . "\0" . $rsa);
    } else {
        throw new UserError('This passkey uses a key type that is not supported. Try a different device or password manager.');
    }
    return "-----BEGIN PUBLIC KEY-----\n" . chunk_split(base64_encode($der), 64, "\n") . "-----END PUBLIC KEY-----\n";
}

function parse_auth_data(string $ad): array
{
    if (strlen($ad) < 37) {
        throw new UserError('The passkey response was malformed.');
    }
    $r = ['rp_id_hash' => substr($ad, 0, 32), 'flags' => ord($ad[32]), 'sign_count' => unpack('N', substr($ad, 33, 4))[1]];
    if ($r['flags'] & 0x40) { // attested credential data present
        $len = unpack('n', substr($ad, 53, 2))[1];
        $r['credential_id'] = substr($ad, 55, $len);
        $offset = 55 + $len;
        $r['public_key'] = Cbor::decode($ad, $offset);
    }
    return $r;
}

function rp_id(): string
{
    if (!empty(config()['rp_id'])) {
        return (string) config()['rp_id'];
    }
    $host = strtolower((string) ($_SERVER['HTTP_HOST'] ?? 'localhost'));
    return preg_replace('/:\d+$/', '', $host);
}

function expected_origin(): string
{
    if (!empty(config()['origin'])) {
        return rtrim((string) config()['origin'], '/');
    }
    return (is_https() ? 'https' : 'http') . '://' . strtolower((string) ($_SERVER['HTTP_HOST'] ?? 'localhost'));
}

function new_challenge(string $purpose): string
{
    $c = random_bytes(32);
    $_SESSION['webauthn'] = ['purpose' => $purpose, 'challenge' => $c, 'expires' => time() + 300];
    return b64url_encode($c);
}

function take_challenge(string $purpose): string
{
    $w = $_SESSION['webauthn'] ?? null;
    unset($_SESSION['webauthn']);
    if (!$w || $w['purpose'] !== $purpose || $w['expires'] < time()) {
        throw new UserError('That took too long — please try again.');
    }
    return $w['challenge'];
}

function check_client_data(string $json, string $type, string $challenge): void
{
    $cd = json_decode($json, true);
    if (!is_array($cd) || ($cd['type'] ?? '') !== $type) {
        throw new UserError('Unexpected passkey response.');
    }
    if (!hash_equals(b64url_encode($challenge), (string) ($cd['challenge'] ?? ''))) {
        throw new UserError('The passkey challenge did not match — please try again.');
    }
    if (($cd['origin'] ?? '') !== expected_origin()) {
        throw new UserError('This passkey request came from an unexpected address (' . ($cd['origin'] ?? '?') . ').');
    }
}

function check_auth_flags(array $ad): void
{
    if (!hash_equals(hash('sha256', rp_id(), true), $ad['rp_id_hash'])) {
        throw new UserError('This passkey belongs to a different website.');
    }
    if (!($ad['flags'] & 0x01) || !($ad['flags'] & 0x04)) {
        throw new UserError('Your device did not confirm it was you (fingerprint, face or PIN). Please try again.');
    }
}

function webauthn_user_id(): string
{
    $id = setting('webauthn_user_id');
    if (!$id) {
        $id = b64url_encode(random_bytes(16));
        set_setting('webauthn_user_id', $id);
    }
    return $id;
}

function passkey_registration_options(): array
{
    $exclude = array_map(fn($id) => ['type' => 'public-key', 'id' => $id],
        db()->query('SELECT credential_id FROM passkeys')->fetchAll(PDO::FETCH_COLUMN));
    $name = trim((string) setting('your_name')) ?: 'Me';
    return [
        'challenge' => new_challenge('register'),
        'rp' => ['id' => rp_id(), 'name' => APP_NAME],
        'user' => ['id' => webauthn_user_id(), 'name' => $name, 'displayName' => $name],
        'pubKeyCredParams' => [['type' => 'public-key', 'alg' => -7], ['type' => 'public-key', 'alg' => -257]],
        'authenticatorSelection' => ['residentKey' => 'required', 'requireResidentKey' => true, 'userVerification' => 'required'],
        'attestation' => 'none',
        'timeout' => 120000,
        'excludeCredentials' => $exclude,
    ];
}

function passkey_register(array $in): array
{
    $challenge = take_challenge('register');
    $clientData = b64url_decode((string) ($in['clientDataJSON'] ?? ''));
    check_client_data($clientData, 'webauthn.create', $challenge);
    $att = Cbor::decode(b64url_decode((string) ($in['attestationObject'] ?? '')));
    if (!is_array($att) || !isset($att['authData'])) {
        throw new UserError('The passkey response was malformed.');
    }
    $ad = parse_auth_data($att['authData']);
    check_auth_flags($ad);
    if (empty($ad['credential_id']) || !is_array($ad['public_key'] ?? null)) {
        throw new UserError('The passkey did not include its public key.');
    }
    $credId = b64url_encode($ad['credential_id']);
    if (!hash_equals($credId, (string) ($in['id'] ?? ''))) {
        throw new UserError('The passkey response was inconsistent.');
    }
    $pem = cose_to_pem($ad['public_key']);
    $name = trim((string) ($in['name'] ?? '')) ?: 'Passkey';
    db()->prepare('INSERT INTO passkeys (credential_id, public_key, sign_count, name) VALUES (?, ?, ?, ?)')
        ->execute([$credId, $pem, $ad['sign_count'], preg_match('/^.{0,60}/us', $name, $m) ? $m[0] : 'Passkey']);
    return ['id' => (int) db()->lastInsertId(), 'name' => $name];
}

function passkey_login_options(): array
{
    // No allow-list: passkeys are discoverable, so the browser offers the ones it has.
    return ['challenge' => new_challenge('login'), 'rpId' => rp_id(), 'userVerification' => 'required', 'timeout' => 120000];
}

function passkey_verify_login(array $in): void
{
    $challenge = take_challenge('login');
    $s = db()->prepare('SELECT * FROM passkeys WHERE credential_id = ?');
    $s->execute([(string) ($in['id'] ?? '')]);
    $cred = $s->fetch();
    if (!$cred) {
        throw new UserError('That passkey is not registered here. Use the email backup to sign in, then add it in Settings.');
    }
    $clientData = b64url_decode((string) ($in['clientDataJSON'] ?? ''));
    check_client_data($clientData, 'webauthn.get', $challenge);
    $authData = b64url_decode((string) ($in['authenticatorData'] ?? ''));
    $ad = parse_auth_data($authData);
    check_auth_flags($ad);
    if (!empty($in['userHandle']) && !hash_equals(webauthn_user_id(), (string) $in['userHandle'])) {
        throw new UserError('That passkey belongs to a different account.');
    }
    $ok = openssl_verify($authData . hash('sha256', $clientData, true), b64url_decode((string) ($in['signature'] ?? '')),
        $cred['public_key'], OPENSSL_ALGO_SHA256);
    if ($ok !== 1) {
        throw new UserError('The passkey signature could not be verified.');
    }
    // Synced passkeys always report 0; a counter that goes backwards suggests a cloned key.
    if (($ad['sign_count'] > 0 || $cred['sign_count'] > 0) && $ad['sign_count'] <= (int) $cred['sign_count']) {
        throw new UserError('This passkey looks like it may have been copied. Sign in with the email backup and replace it.');
    }
    db()->prepare('UPDATE passkeys SET sign_count = ?, last_used_at = CURRENT_TIMESTAMP WHERE id = ?')->execute([$ad['sign_count'], $cred['id']]);
}
