#!/usr/bin/env bash
# Installs (or updates) Timesheets on AlmaLinux / RHEL 9–10 with nginx and PHP-FPM.
#
#   sudo ./deploy/install-almalinux.sh --domain time.example.com --cert-email you@example.com
#
# Safe to run again to update the app: your config.php and data are kept.
# Adds one nginx site and one PHP-FPM pool; your existing site is not touched.
#
# Options:
#   --domain NAME        subdomain to serve the app on (required)
#   --cert-email EMAIL   email for Let's Encrypt expiry notices (required unless --no-certbot)
#   --app-dir DIR        where the app goes            (default /var/www/timetracker)
#   --data-dir DIR       where the database goes       (default /var/lib/timetracker)
#   --no-certbot         don't request a certificate; you must already have one at
#                        /etc/letsencrypt/live/DOMAIN/ (or edit the nginx file afterwards)
set -euo pipefail

DOMAIN=""
CERT_EMAIL=""
APP_DIR="/var/www/timetracker"
DATA_DIR="/var/lib/timetracker"
CERTBOT=1
SRC="$(cd "$(dirname "$0")/.." && pwd)"
SOCKET="/run/php-fpm/timetracker.sock"
ACME_ROOT="/var/www/letsencrypt"
NGINX_CONF="/etc/nginx/conf.d/timetracker.conf"
POOL_CONF="/etc/php-fpm.d/timetracker.conf"

while [ $# -gt 0 ]; do
    case "$1" in
        --domain) DOMAIN="$2"; shift 2 ;;
        --cert-email) CERT_EMAIL="$2"; shift 2 ;;
        --app-dir) APP_DIR="${2%/}"; shift 2 ;;
        --data-dir) DATA_DIR="${2%/}"; shift 2 ;;
        --no-certbot) CERTBOT=0; shift ;;
        -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
        *) echo "Unknown option: $1 (see --help)"; exit 1 ;;
    esac
done

say()  { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[1;33mWarning:\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31mError:\033[0m %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "Run this with sudo."
[ -n "$DOMAIN" ] || die "--domain is required, e.g. --domain time.example.com"
[[ "$DOMAIN" =~ ^[A-Za-z0-9.-]+$ ]] || die "That doesn't look like a domain name: $DOMAIN"
[ "$CERTBOT" -eq 0 ] || [ -n "$CERT_EMAIL" ] || die "--cert-email is required for Let's Encrypt (or use --no-certbot)."
[ -f "$SRC/index.php" ] && [ -f "$SRC/lib/app.php" ] || die "Run this from inside the timetracker folder you downloaded."
# Both folders must be absolute and dedicated: the app folder is synced with --delete.
case "$APP_DIR" in /?*) ;; *) die "--app-dir must be an absolute path, e.g. /var/www/timetracker" ;; esac
case "$DATA_DIR" in /?*) ;; *) die "--data-dir must be an absolute path, e.g. /var/lib/timetracker" ;; esac
case "$APP_DIR" in /bin|/boot|/dev|/etc|/home|/lib|/lib64|/opt|/proc|/root|/run|/sbin|/srv|/sys|/tmp|/usr|/usr/*|/var|/var/www|/var/lib|/var/log|"$SRC"|"$SRC"/*)
    die "Refusing to install into $APP_DIR — choose a dedicated folder such as /var/www/timetracker." ;; esac
case "$DATA_DIR" in /bin|/boot|/dev|/etc|/home|/lib|/lib64|/opt|/proc|/root|/run|/sbin|/srv|/sys|/tmp|/usr|/usr/*|/var|/var/www|/var/lib|/var/log)
    die "Refusing to use $DATA_DIR for data — choose a dedicated folder such as /var/lib/timetracker." ;; esac
case "$DATA_DIR" in "$APP_DIR"|"$APP_DIR"/*) die "Keep --data-dir outside --app-dir so the database can't be served." ;; esac
command -v dnf >/dev/null || die "This script is for AlmaLinux/RHEL (dnf not found)."

# ---------------------------------------------------------------------------
say "Installing packages (nginx, PHP-FPM with SQLite)"
FPM_WAS_RUNNING=0
systemctl is-active --quiet php-fpm 2>/dev/null && FPM_WAS_RUNNING=1
dnf install -y -q nginx php-fpm php-cli php-pdo php-opcache rsync policycoreutils-python-utils
php -m | grep -qi pdo_sqlite || die "PHP's SQLite driver (pdo_sqlite) is missing — try: dnf install php-pdo"
PHP_VER="$(php -r 'echo PHP_VERSION;')"
php -r 'exit(version_compare(PHP_VERSION, "8.1", ">=") ? 0 : 1);' || die "PHP 8.1+ is needed (found $PHP_VER)."
if [ "$CERTBOT" -eq 1 ] && ! command -v certbot >/dev/null; then
    dnf install -y -q epel-release
    dnf install -y -q certbot
fi

# ---------------------------------------------------------------------------
say "Creating the timetracker user and folders"
id timetracker >/dev/null 2>&1 || useradd --system --home-dir "$DATA_DIR" --shell /sbin/nologin timetracker
mkdir -p "$APP_DIR" "$DATA_DIR/sessions" "$ACME_ROOT"
chown -R timetracker:timetracker "$DATA_DIR"
chmod 700 "$DATA_DIR" "$DATA_DIR/sessions"

say "Copying the app to $APP_DIR"
rsync -a --delete --exclude '/data/' --exclude '/config.php' --exclude '/deploy/' --exclude '.git*' \
    --exclude '/start.sh' --exclude '/start.bat' --exclude '/router.php' "$SRC/" "${APP_DIR:?}/"
chown -R root:root "$APP_DIR"
find "$APP_DIR" -type d -exec chmod 755 {} +
find "$APP_DIR" -type f -exec chmod 644 {} +

# ---------------------------------------------------------------------------
if [ ! -f "$APP_DIR/config.php" ]; then
    say "Sign-in email and Brevo settings (saved to $APP_DIR/config.php)"
    echo "These stay on the server: they are never shown in the app or included in backups."
    read -r -p "Email address for sign-in codes: " OWNER_EMAIL
    echo "How does Brevo send for you?  1) Brevo API key (xkeysib-…)   2) Brevo SMTP relay (login + SMTP key)"
    read -r -p "Choose 1 or 2 [1]: " BREVO_MODE
    read -r -p "Sender address — must be a sender/domain verified in Brevo (e.g. noreply@yourdomain.com): " MAIL_FROM
    if [ "${BREVO_MODE:-1}" = "2" ]; then
        read -r -p "Brevo SMTP login (looks like 1234ab@smtp-brevo.com): " SMTP_USER
        read -r -s -p "Brevo SMTP key (hidden): " SMTP_KEY; echo
        MAIL_BLOCK="'mail' => [
        'transport' => 'smtp',
        'host' => 'smtp-relay.brevo.com',
        'port' => 587,
        'secure' => 'tls',
        'username' => $(php -r 'echo var_export($argv[1], true);' "$SMTP_USER"),
        'password' => $(php -r 'echo var_export($argv[1], true);' "$SMTP_KEY"),
        'from' => $(php -r 'echo var_export($argv[1], true);' "$MAIL_FROM"),
    ],"
    else
        read -r -s -p "Brevo API key (hidden): " API_KEY; echo
        MAIL_BLOCK="'mail' => [
        'transport' => 'brevo',
        'api_key' => $(php -r 'echo var_export($argv[1], true);' "$API_KEY"),
        'from' => $(php -r 'echo var_export($argv[1], true);' "$MAIL_FROM"),
    ],"
    fi
    umask 027
    cat > "$APP_DIR/config.php" <<PHP
<?php
// Private settings for Timesheets. Edit with: sudo nano $APP_DIR/config.php
// See config.sample.php for every option.
return [
    'owner_email' => $(php -r 'echo var_export($argv[1], true);' "$OWNER_EMAIL"),
    $MAIL_BLOCK
    'data_dir' => '$DATA_DIR',
];
PHP
    umask 022
    php -l "$APP_DIR/config.php" >/dev/null || die "config.php has a syntax error — please edit it by hand."
else
    say "Keeping your existing $APP_DIR/config.php"
fi
# Readable by PHP (timetracker) but not by nginx or other users.
chown root:timetracker "$APP_DIR/config.php"
chmod 640 "$APP_DIR/config.php"

# ---------------------------------------------------------------------------
say "Setting up the PHP-FPM pool (ondemand: no memory used while idle)"
sed -e "s#__APP_DIR__#$APP_DIR#g" -e "s#__DATA_DIR__#$DATA_DIR#g" "$SRC/deploy/php-fpm-timetracker.conf" > "$POOL_CONF"
# A fresh php-fpm starts a "www" pool with 5 idle workers. If nothing else used
# PHP before, make it ondemand too so it costs nothing while idle.
if [ "$FPM_WAS_RUNNING" -eq 0 ] && [ -f /etc/php-fpm.d/www.conf ] && grep -q '^pm = dynamic' /etc/php-fpm.d/www.conf; then
    sed -i -e 's/^pm = dynamic/pm = ondemand/' -e 's/^pm.max_children = .*/pm.max_children = 2/' /etc/php-fpm.d/www.conf
    echo "Set the unused default 'www' pool to ondemand to save memory."
fi
php-fpm -t
systemctl enable -q php-fpm
# Reload (not restart) so a site already using PHP-FPM isn't interrupted.
if [ "$FPM_WAS_RUNNING" -eq 1 ]; then systemctl reload php-fpm; else systemctl restart php-fpm; fi

# ---------------------------------------------------------------------------
if command -v selinuxenabled >/dev/null && selinuxenabled; then
    say "Configuring SELinux"
    semanage fcontext -a -t httpd_sys_rw_content_t "$DATA_DIR(/.*)?" 2>/dev/null \
        || semanage fcontext -m -t httpd_sys_rw_content_t "$DATA_DIR(/.*)?"
    case "$APP_DIR" in
        /var/www/*) ;;
        *) semanage fcontext -a -t httpd_sys_content_t "$APP_DIR(/.*)?" 2>/dev/null \
               || semanage fcontext -m -t httpd_sys_content_t "$APP_DIR(/.*)?" ;;
    esac
    restorecon -R "$DATA_DIR" "$APP_DIR" "$ACME_ROOT"
    # PHP needs to connect out to Brevo (HTTPS API or SMTP port 587).
    setsebool -P httpd_can_network_connect on
fi

if systemctl is-active --quiet firewalld; then
    say "Opening HTTP/HTTPS in the firewall"
    firewall-cmd -q --permanent --add-service=http --add-service=https
    firewall-cmd -q --reload
fi

# ---------------------------------------------------------------------------
systemctl enable -q nginx
systemctl is-active --quiet nginx || systemctl start nginx
CERT_DIR="/etc/letsencrypt/live/$DOMAIN"
if [ ! -f "$CERT_DIR/fullchain.pem" ]; then
    [ "$CERTBOT" -eq 1 ] || die "No certificate at $CERT_DIR. Get one first, or drop --no-certbot."
    say "Getting a Let's Encrypt certificate for $DOMAIN"
    # Temporary HTTP-only site so Let's Encrypt can check the domain points here.
    cat > "$NGINX_CONF" <<NGINX
server {
    listen 80;
    listen [::]:80;
    server_name $DOMAIN;
    location ^~ /.well-known/acme-challenge/ { root $ACME_ROOT; }
    location / { return 404; }
}
NGINX
    nginx -t -q && systemctl reload nginx
    certbot certonly --webroot -w "$ACME_ROOT" -d "$DOMAIN" --email "$CERT_EMAIL" --agree-tos -n \
        --deploy-hook "systemctl reload nginx" \
        || die "Certificate request failed. Check that $DOMAIN's DNS points to this server and port 80 is open."
fi

say "Writing the nginx site"
sed -e "s#__DOMAIN__#$DOMAIN#g" -e "s#__APP_DIR__#$APP_DIR#g" -e "s#__SOCKET__#$SOCKET#g" \
    "$SRC/deploy/nginx-timetracker.conf" > "$NGINX_CONF"
# Servers with IPv6 switched off can't listen on [::].
[ -f /proc/net/if_inet6 ] || sed -i '/listen \[::\]/d' "$NGINX_CONF"
# "http2 on;" needs nginx 1.25.1+ (AlmaLinux 10 has 1.26); older versions use "listen … http2".
NGINX_VER="$(nginx -v 2>&1 | sed -n 's#.*nginx/\([0-9.]*\).*#\1#p')"
if [ "$(printf '%s\n' 1.25.1 "$NGINX_VER" | sort -V | head -1)" != 1.25.1 ]; then
    sed -i -e '/http2 on;/d' -e 's/listen 443 ssl;/listen 443 ssl http2;/' -e 's/listen \[::\]:443 ssl;/listen [::]:443 ssl http2;/' "$NGINX_CONF"
fi
nginx -t -q || die "nginx rejected the configuration (see above); your previous sites are unaffected until it's fixed."
systemctl reload nginx

# ---------------------------------------------------------------------------
say "Done"
cat <<EOF
Timesheets is at:  https://$DOMAIN/

Next:
  1. Open it, click "Email me a setup code", enter the code and choose a password.
  2. In Settings, click "Add a passkey", then fill in "Your details".
  3. Settings → Email delivery → "Send a test email" checks Brevo is working.

Files:
  app        $APP_DIR      (config: $APP_DIR/config.php)
  data       $DATA_DIR     (back up timetracker.sqlite, or use Settings → Backup)
  nginx      $NGINX_CONF
  php-fpm    $POOL_CONF
  PHP errors $DATA_DIR/php-errors.log

To update later: download the new version and run this script again with the same options.
EOF
echo
free -m | awk 'NR==1 || /^Mem/'
