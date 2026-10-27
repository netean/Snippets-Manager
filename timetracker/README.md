# Timetracker

A small, private, single-user time tracker and invoicing app for freelancers and contractors.
It takes ideas from Clockify and solidtime and leaves out the team features.

* **PHP 8.1+ and SQLite**: no framework, no build step, no database server. All your data is in one file.
* **Light**: runs on a 1 GB, 1 CPU server alongside another website, and uses no memory while idle.
* **Runs on a subdomain** of your website, e.g. `time.yourdomain.com`, behind passkey sign-in.

**Contents**
1. [What it does](#what-it-does)
2. [Deploying to your server](#deploying-to-your-server-almalinux-10--nginx)
   1. [How the subdomain works](#how-the-subdomain-works)
   2. [Step 1: add the DNS record](#step-1-add-the-dns-record-for-the-subdomain)
   3. [Step 2: let the server download this private repo](#step-2-let-the-server-download-this-private-repo)
   4. [Step 3: have your Brevo details ready](#step-3-have-your-brevo-details-ready)
   5. [Step 4: run the installer](#step-4-run-the-installer)
   6. [Step 5: first sign-in](#step-5-first-sign-in)
   7. [Updating](#updating)
   8. [Where everything lives](#where-everything-lives)
   9. [Backups](#backups)
   10. [Troubleshooting](#troubleshooting)
3. [Memory and CPU use](#memory-and-cpu-use)
4. [Everyday use](#everyday-use)
5. [Other ways to run it](#other-ways-to-run-it)
6. [Files](#files)

---

## What it does

| | |
|---|---|
| **Track** | One-click timer, kept on the server so you can start it on one machine and stop it on another, or manual entry. Durations can be typed as `1:30`, `1.5`, `1h30` or `90m`, and times as `9`, `930` or `2pm`. Entries are grouped by day with daily and weekly totals. Each entry has buttons to restart its timer, copy it to today, or delete it. |
| **Past time** | "Add past time" works for any date. Multi-select entries to move them to another project or date, add or remove categories, mark them billable or not, copy them or delete them. |
| **Bulk entries** | *Bulk add* creates the same entry on chosen weekdays across a date range, e.g. 7.5h a day on Project X for all of last week, and can skip days that already have time. The **Timesheet** tab is a week grid you can type hours straight into. *Copy rows from last week* fills in your usual projects. |
| **Organise** | Clients → projects → tasks. A task with no project is a general work item that works on any project. Entries can have **several categories**. One piece of work can be logged against **several projects** at once, with the time either split evenly between them or logged in full against each. |
| **Rates & billing basis** | Each client is billed **hourly**, by the **half day** or by the **full day**, at its own rate. Hourly clients can have rates overridden per project, task or entry (the rate used is: entry → task → project → client → default). For half-day and day clients, time is added up per client per date. **Half-day billing:** up to half a working day (7.5h by default, set per client) bills ½ day, and more bills a full day. **Day billing:** any time on a date bills 1 day. A date never bills more than one day, and 15 minutes across two of the same client's projects is still one half day. |
| **Invoices** | Bill one client, or **all clients on one invoice** (for an umbrella company). You choose the period and how lines are grouped (per project, project & task, day, or entry). You can add expense lines. Tax/VAT is set per client or by default. Invoice numbers run automatically. Status goes draft → sent → paid, with overdue highlighting. Invoiced time is locked so it can't be billed twice; deleting an invoice releases its time. |
| **Timesheets & reports** | Printable weekly/monthly timesheets with an optional client sign-off box, a detailed report broken down by client, project, task, category and day, and CSV export. Invoices can carry a timesheet appendix. Use your browser's *Print → Save as PDF* to send any of them. |
| **Sign-in** | Everything is behind sign-in. You sign in with a **passkey** (fingerprint, face or device PIN). The backup is your **password plus a 6-digit code emailed to you**. First-time setup also needs an emailed code, so nobody else can claim your site. You get an email whenever a passkey is added. Your sign-in email address and Brevo details live only in `config.php` on the server; they are never shown in the app, included in backups or committed to git. |
| **Safety** | CSRF protection, lock-out after 5 wrong passwords, limits on code attempts and resends, private files blocked from the web, and a one-click database backup. |

---

## Deploying to your server (AlmaLinux 10 + nginx)

These steps are for the existing server:
* **AlmaLinux 10** with **nginx 1.26** and **PHP 8.3**, already hosting a website at `/var/www/consultant-site/site`.
* `/root/deploy` for deployments and `/root/tmp` for temporary files.

The app goes **alongside** your website and doesn't change it. Everything below is run as **root** on the server.

In the examples, replace `yourdomain.com` with your real domain and `time` with whatever subdomain you'd like (`time`, `timesheets`, `work`…).

### How the subdomain works

On your own server there's no hosting control panel to "create" a subdomain. A subdomain is just two things:

1. **A DNS record** that tells the internet `time.yourdomain.com` lives at your server's IP address. You add it with whoever manages your domain's DNS.
2. **An nginx site** on the server that answers requests for `time.yourdomain.com`. nginx already serves your main site; it looks at the name each browser asks for and hands `time.yourdomain.com` to Timetracker instead. The installer adds this site and gets an HTTPS certificate for it.

```
Browser ──► DNS: time.yourdomain.com = your server's IP
        ──► your server ──► nginx
                             ├─ yourdomain.com       → /var/www/consultant-site/site   (unchanged)
                             └─ time.yourdomain.com  → /var/www/timetracker/site       (new)
```

The subdomain gets its own HTTPS certificate and its own sign-in cookie, which your main site can't see.

### Step 1: add the DNS record for the subdomain

**1. Find your server's IP address.** On the server:

```bash
curl -4 -s https://ifconfig.co        # IPv4 address, e.g. 203.0.113.10
curl -6 -s https://ifconfig.co        # IPv6 address, if the server has one (may just fail — that's fine)
```

You can also see it in your VPS provider's control panel.

**2. Find where your domain's DNS is managed.** It's usually your domain registrar (e.g. Namecheap, GoDaddy, 123-reg, IONOS), or Cloudflare if you moved your DNS there. It's wherever your main site's records already are. Look for a page called *DNS*, *DNS records*, *Zone editor* or *Advanced DNS*.

**3. Add an A record:**

| Type | Name / Host | Value / Points to | TTL |
|---|---|---|---|
| `A` | `time` | your server's IPv4 address, e.g. `203.0.113.10` | Auto, or 300 (5 minutes) |

* In *Name*, enter just `time`, not the whole `time.yourdomain.com`. Most providers add the domain for you. Some want `time.yourdomain.com.` with a trailing dot; their form usually shows an example.
* **If the server has an IPv6 address**, add a second record: type `AAAA`, name `time`, value the IPv6 address. If you leave it out, IPv6 users simply connect over IPv4. If you add one, it must be the right address, or certificate checks can fail.
* **Using Cloudflare?** Set the record to **DNS only** (grey cloud), not *Proxied* (orange cloud). This keeps certificates and passkeys simple.
* **CAA records:** if your domain has any `CAA` records, one must allow `letsencrypt.org`. If you have none, there's nothing to do.

Alternatively, if your main site's `A` record already points at this same server, you can add a `CNAME` named `time` with the value `yourdomain.com` instead. The `A` record above is the simplest option.

**4. Wait for it to work.** This usually takes a few minutes, occasionally a few hours. Check from the server or your own computer:

```bash
dig +short time.yourdomain.com A        # should print your server's IPv4 address
# (if dig is missing: dnf install -y bind-utils — or use: getent hosts time.yourdomain.com)
```

Don't run the installer until this prints your server's address. Let's Encrypt has to reach the subdomain to issue the certificate.

**5. Make sure ports 80 and 443 are open.** They will be if your main site already works over HTTPS. The installer also opens them in `firewalld`. If your VPS provider has its own firewall in its control panel, it must allow 80 and 443 too.

### Step 2: let the server download this private repo

This repo is private, so the server needs permission to read it. A **deploy key** is the safest way: it is read-only and only works for this one repository.

```bash
# 1. Make a key just for this repo (no passphrase, so updates can run unattended)
ssh-keygen -t ed25519 -f /root/.ssh/timetracker_deploy -N "" -C "timetracker deploy key"

# 2. Tell SSH to use it for this repo
cat >> /root/.ssh/config <<'EOF'

Host github-timetracker
    HostName github.com
    User git
    IdentityFile /root/.ssh/timetracker_deploy
    IdentitiesOnly yes
EOF
chmod 600 /root/.ssh/config

# 3. Show the public key, then copy it
cat /root/.ssh/timetracker_deploy.pub
```

On GitHub, open this repository → **Settings** → **Deploy keys** → **Add deploy key**. Give it a title (e.g. "consultant server"), paste the key and leave **Allow write access unticked**. Click **Add key**.

Then download the app into your deploy folder:

```bash
dnf install -y git
git clone git@github-timetracker:netean/Timetracker.git /root/deploy/timetracker
```

The first time, SSH asks whether to trust `github.com`; type `yes`.

### Step 3: have your Brevo details ready

Sign-in codes are sent through your existing Brevo account. You need **one** of:

* **A Brevo API key** (recommended): Brevo → *SMTP & API* → *API keys* → *Generate a new API key*. It starts `xkeysib-`.
* **Or Brevo SMTP relay details**: Brevo → *SMTP & API* → *SMTP*. Note the **Login** (looks like `1234ab@smtp-brevo.com`), then generate an **SMTP key**.

You also need a **sender address Brevo has verified**, e.g. `noreply@yourdomain.com`. Set this up under Brevo → *Senders, domains & dedicated IPs*. If your domain is already authenticated in Brevo for your main site, any address at that domain works.

### Step 4: run the installer

```bash
cd /root/deploy/timetracker
./deploy/install-almalinux.sh \
    --domain time.yourdomain.com \
    --cert-email you@yourdomain.com \
    --app-dir /var/www/timetracker/site
```

* `--domain` is the subdomain from Step 1.
* `--cert-email` is where Let's Encrypt sends certificate expiry warnings. It isn't stored in the app.
* `--app-dir /var/www/timetracker/site` matches the layout of your existing site. The database goes in `/var/lib/timetracker`, outside the web folders.

It asks four questions, which are saved only to `config.php` on the server:
1. **Email address for sign-in codes**: the address where you want sign-in codes sent (e.g. your Gmail)
2. **Brevo method**: `1` for API key, `2` for SMTP relay
3. **Sender address**: your Brevo-verified sender, e.g. `noreply@yourdomain.com`
4. **Brevo API key**, or SMTP login and key. Typing is hidden.

What it does:
* installs anything missing (nginx, PHP-FPM and PHP's SQLite driver are probably already there; certbot comes from EPEL);
* creates a `timetracker` system user, so the app runs separately from your main site;
* copies the app to `/var/www/timetracker/site` and creates `/var/lib/timetracker` for the database;
* adds a PHP-FPM pool at `/etc/php-fpm.d/timetracker.conf`. If PHP-FPM is already running for your main site, it *reloads* rather than restarts, so your site isn't interrupted;
* gets a Let's Encrypt certificate for the subdomain, which renews automatically;
* adds the nginx site `/etc/nginx/conf.d/timetracker.conf`, checks the whole nginx config with `nginx -t` and only then reloads nginx;
* sets SELinux labels so PHP can write the database, allows PHP to connect to Brevo (`httpd_can_network_connect`), and opens HTTP/HTTPS in `firewalld`.

At the end it prints the address, where everything is, and the server's free memory.

### Step 5: first sign-in

1. Open `https://time.yourdomain.com/`.
2. Click **Email me a setup code**. Enter the 6-digit code from your email and choose a password (10+ characters).
3. You're taken to **Settings**:
   * **Sign-in & security → Add a passkey.** Your browser asks for your fingerprint, face or PIN. Add one on each device, or use a password manager / iCloud Keychain / Google Password Manager so one passkey syncs everywhere.
   * **Your details**: full name, company name, contact email (printed on invoices), address, VAT number. Click *Save*.
   * **Rates & invoicing**: currency, default rates, VAT, invoice numbering, bank details.
   * **Email delivery → Send a test email** checks Brevo is working.
4. From then on, sign in with your passkey. If you can't, use **"Can't use your passkey?"**: your password plus an emailed code.

### Updating

```bash
cd /root/deploy/timetracker
git pull
./deploy/install-almalinux.sh --domain time.yourdomain.com --cert-email you@yourdomain.com --app-dir /var/www/timetracker/site
```

Always use the same options. Your `config.php`, database and passkeys are kept, and browsers pick up the new version automatically.

### Where everything lives

| What | Where |
|---|---|
| The app | `/var/www/timetracker/site` |
| Private settings (sign-in email, Brevo key) | `/var/www/timetracker/site/config.php` — edit with `nano`; changes apply immediately |
| Database | `/var/lib/timetracker/timetracker.sqlite` |
| PHP errors (only created if there are any) | `/var/lib/timetracker/php-errors.log` |
| nginx site | `/etc/nginx/conf.d/timetracker.conf` |
| PHP-FPM pool | `/etc/php-fpm.d/timetracker.conf` |
| Certificate | `/etc/letsencrypt/live/time.yourdomain.com/` |
| Source / installer | `/root/deploy/timetracker` |

### Backups

All your data is the one database file. You can download a copy any time from **Settings → Backup & export**. For automatic nightly copies on the server:

```bash
dnf install -y sqlite
mkdir -p /root/backups/timetracker
crontab -e
```

and add this line. It keeps 30 days of backups; note the `\%`, which cron needs:

```
15 3 * * * sqlite3 /var/lib/timetracker/timetracker.sqlite ".backup '/root/backups/timetracker/timetracker-$(date +\%F).sqlite'" && find /root/backups/timetracker -name '*.sqlite' -mtime +30 -delete
```

Also keep a copy of `config.php` somewhere safe. It isn't in the database or in git.

### Troubleshooting

| Problem | What to check |
|---|---|
| Installer says the certificate request failed | `dig +short time.yourdomain.com` must show this server's IP. Ports 80 and 443 must be open, including any firewall in your VPS provider's panel. Cloudflare must be set to *DNS only*. A wrong `AAAA` record also breaks it. Fix the problem and re-run the installer. |
| "Sign-in emails are not set up yet" | `config.php` is missing `owner_email`. Edit `/var/www/timetracker/site/config.php`. |
| Test email fails | The message names the cause. Usually the sender isn't verified in Brevo, or the key is wrong or was revoked. If it mentions connection or permission errors, check `getsebool httpd_can_network_connect` shows `on`. |
| 502 Bad Gateway | `systemctl status php-fpm` and `ls -l /run/php-fpm/timetracker.sock`. Then check `tail /var/log/nginx/error.log`. |
| Something blocked by SELinux | `ausearch -m avc -ts recent`, then re-run the installer, which re-applies the labels. |
| Errors in the app | `/var/lib/timetracker/php-errors.log` and `/var/log/nginx/error.log` |
| Passkey button does nothing / errors | Passkeys only work over `https://` at the subdomain address, not an IP address. |
| Locked out completely (no passkey, forgot password) | `sqlite3 /var/lib/timetracker/timetracker.sqlite "DELETE FROM settings WHERE key='password_hash';"`, then visit the site and set up again with an emailed code. Your data and passkeys are kept. |

To remove the app: delete `/etc/nginx/conf.d/timetracker.conf` and `/etc/php-fpm.d/timetracker.conf`, then run `systemctl reload nginx php-fpm`. After that, delete the app and data folders if you no longer need them.

---

## Memory and CPU use

Built to share a 1 GB, single-CPU server:

* **No database server and no background jobs.** SQLite is just a file.
* **No PHP processes while you're not using it.** The PHP-FPM pool is "on demand": loading a page starts one worker (about 20 MB), which exits after 10 idle seconds. At most 2 run at once, each capped at 64 MB.
* **Flat memory as your history grows.** Reports, exports and invoices read entries one at a time. Tested with 25,000 entries (about ten years of use): an all-time report peaks at about 4 MB, and the CSV and printable reports at about 2 MB.
* **nginx serves CSS and JavaScript directly**, and browsers cache them for a week.
* If PHP-FPM wasn't running before the install, the installer also switches its default `www` pool to on-demand, so it doesn't keep 5 idle processes around. If your main site uses PHP-FPM, that pool is left alone.

To see it for yourself: `ps -o rss,cmd -C php-fpm`. RSS is in KB; with the app idle, you'll see no `pool timetracker` processes.

---

## Everyday use

* **Weekly timesheet for a client:** *Reports* → choose the client and *Last week* → *🖨 Timesheet*. Add the sign-off box and amounts there if you need them.
* **Monthly invoice:** *Invoices* → choose the client (or *All clients* for an umbrella company) and *Last month* → check the preview → *Create invoice*. The invoice opens ready to save as PDF.
* **Clients billed by the day:** in *Clients & projects*, set *Bill by* to *Half days* or *Full days*. The client's rate is then their **day rate**.
* **Lost a passkey?** Use "Can't use your passkey?" on the sign-in page, then remove the old passkey and add a new one in Settings.

---

## Other ways to run it

**Shared web hosting (Apache/LiteSpeed):**
1. Create the subdomain in your hosting panel and turn on HTTPS.
2. Copy `config.sample.php` to `config.php`. Set `owner_email`, your mail details and a `data_dir` outside the web root.
3. Upload everything. The bundled `.htaccess` files block private files.

**On one computer or a shared drive:** install PHP 8.1+ with SQLite and create `config.php`. Then run `./start.sh` (Linux/macOS) or `start.bat` (Windows) and open `http://localhost:8765/`. On Windows, you can unzip a portable PHP into a `php` folder next to `start.bat`.

---

## Files

```
index.php           the app (single page)
api.php             JSON API used by the app
auth.php            sign-in API (passkeys, emailed codes, first-run setup)
login.php           sign-in page
invoice.php         printable invoice
report.php          printable timesheets & reports, CSV export
backup.php          database download
config.sample.php   template for config.php (private settings: never committed)
lib/                PHP code: database & billing, passkeys, email (never served)
assets/             CSS and JavaScript
deploy/             AlmaLinux + nginx installer, nginx site, PHP-FPM pool
router.php, start.sh, start.bat   for running locally without a web server
```
