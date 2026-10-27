# Timesheets — personal time tracking & invoicing

A small, single-user time tracker and invoicing tool for freelancers and contractors.
It takes ideas from Clockify and solidtime and leaves out the team features.

* **PHP 8.1+ and SQLite**: no framework, no build step, no database server. All your data is in one file.
* **Runs two ways**: on a subdomain of your website (e.g. `time.example.com`), or from a shared drive with PHP's built-in server.

## What it does

| | |
|---|---|
| **Track** | One-click timer (kept on the server, so you can start it on one machine and stop it on another) or manual entry. Durations can be typed as `1:30`, `1.5`, `1h30` or `90m`, and times as `9`, `930`, `2pm`. Entries are grouped by day with daily and weekly totals. Each entry has buttons to restart its timer, copy it to today, or delete it. |
| **Past time** | "Add past time" works for any date. Multi-select entries to move them to another project or date, add or remove categories, mark them billable or not, copy them or delete them. |
| **Bulk entries** | *Bulk add* creates the same entry on chosen weekdays across a date range, e.g. 7.5h a day on Project X for all of last week, and can skip days that already have time. The **Timesheet** tab is a week grid you can type hours straight into. *Copy rows from last week* fills in your usual projects. |
| **Organise** | Clients → projects → tasks. A task with no project is a general work item that works on any project. Entries can have **several categories**. One piece of work can be logged against **several projects** at once, with the time either split evenly between them or logged in full against each. |
| **Rates & billing basis** | Each client is billed **hourly**, by the **half day** or by the **full day**, at its own rate. Hourly clients can have rates overridden per project, task or entry (the rate used is: entry → task → project → client → default). For half-day and day clients, time is added up per client per date. **Half-day billing:** up to half a working day (7.5h by default, set per client) bills ½ day, and more bills a full day. **Day billing:** any time on a date bills 1 day. A date never bills more than one day, and 15 minutes across two of the same client's projects is still one half day. Invoices show "½ day", "1 day" and so on, and timesheets show what each date was billed as. |
| **Invoices** | Bill one client, or **all clients on one invoice** (for an umbrella company). You choose the period and how lines are grouped (per project, project & task, day, or entry). You can add expense lines. Tax/VAT is set per client or by default. Invoice numbers run automatically. Status goes draft → sent → paid, with overdue highlighting. Invoiced time is locked so it can't be billed twice; deleting an invoice releases its time. |
| **Timesheets & reports** | Printable weekly/monthly timesheets with an optional client sign-off box, a detailed report broken down by client, project, task, category and day, and CSV export. Invoices can carry a timesheet appendix. Use your browser's *Print → Save as PDF* to send any of them. |
| **Sign-in** | Everything is behind sign-in. You sign in with a **passkey** (fingerprint, face or device PIN). The backup is your **password plus a 6-digit code emailed to you**, so it is still two factors. First-time setup also needs an emailed code, so nobody else can claim your site. You get an email whenever a passkey is added. Your email address lives only in `config.php` on the server; it is never shown in the app or committed to git. |
| **Safety** | CSRF protection, lock-out after 5 wrong passwords, limits on code attempts and resends, private folders blocked from the web, and a one-click database backup. |

## Option A — on a subdomain of your website

Works on almost any shared host (cPanel, Plesk, etc.) running Apache or LiteSpeed with PHP 8.1+.

1. In your hosting panel, create a subdomain such as `time.yourdomain.com` and note its document root folder.
2. Turn on HTTPS for the subdomain (most hosts include a free certificate). **Passkeys only work over HTTPS.** Then uncomment the HTTPS lines in `.htaccess`.
3. Copy `config.sample.php` to `config.php` and fill in:
   * `owner_email`: where sign-in codes are sent (e.g. your Gmail address).
   * `mail`: how to send email. Your host's `mail()` usually works. If codes land in spam or don't arrive, use Gmail's SMTP with an App Password; the steps are in the file.
   * `data_dir` (recommended): a folder **outside** the subdomain's document root, e.g. `/home/youraccount/timetracker-data`.
4. Upload the **contents** of this `timetracker` folder, including your `config.php`, into the document root. Make sure the data folder is writable by PHP. It usually is; if not, set it to 755 or 775.
5. Visit `https://time.yourdomain.com/`, click **Email me a setup code**, enter the code and choose a password. You're then taken to Settings: click **Add a passkey**. Add one on each computer or phone you use, or keep passkeys in a password manager or iCloud/Google so they sync.

The bundled `.htaccess` files block web access to `config.php`, `data/` and `lib/`.

*nginx:* add `location ~ ^/(data|lib)/ { deny all; }` and `location ~ ^/(config|router)\.php$ { deny all; }` to the server block.

## Option B — from a shared drive (no web hosting)

1. Put the `timetracker` folder on the shared drive.
2. Each computer needs PHP 8.1+ with the `pdo_sqlite` extension:
   * **Linux:** e.g. `sudo dnf install php-cli php-pdo` or `sudo apt install php-cli php-sqlite3`.
   * **macOS:** `brew install php`.
   * **Windows:** download the "Non Thread Safe" zip from windows.php.net and unzip it into a folder called `php` *inside* the timetracker folder. Then every PC uses that copy and nothing has to be installed.
3. Create `config.php` as above. Email still needs setting up for sign-in codes; Gmail SMTP works well.
4. Double-click `start.bat` (Windows) or run `./start.sh` (Linux/macOS). Your browser opens at `http://localhost:8765/`. Passkeys work there too, but they're tied to the address, so each computer needs its own.

The server only listens on the local computer. The database uses SQLite's standard journal mode rather than WAL, which works on network drives. Only one computer should have it open at a time.

## Everyday tips

* **First steps:** open *Settings* and enter your business details, bank details, default hourly and day rates, VAT and invoice numbering. For each client, choose *Bill by: Hourly / Half days / Full days*. For half-day and day clients, the client's rate is their **day rate**. Then add clients and projects under *Clients & projects*. You can also create a project straight from any project picker ("＋ New project…").
* **Weekly timesheet for a client:** *Reports* → choose the client and *Last week* → *🖨 Timesheet*. Add the sign-off box and amounts there if you need them.
* **Monthly invoice:** *Invoices* → choose the client (or *All clients*) and *Last month* → check the preview → *Create invoice*. The invoice opens in a new tab ready to save as PDF.
* **Back up** now and then from *Settings → Backup & export*. The backup is a normal SQLite file that you can also open with tools like *DB Browser for SQLite*.
* **Lost your passkey?** Use "Can't use your passkey?" on the sign-in page: your password plus an emailed code gets you in. Then remove the old passkey and add a new one in Settings.
* **Forgotten password:** delete the `password_hash` row from the `settings` table (e.g. with DB Browser for SQLite), visit the site and set up again with an emailed code. Your data and passkeys are kept.

## Files

```
index.php        the app (single page)
api.php          JSON API used by the app
auth.php         sign-in API (passkeys, emailed codes, first-run setup)
login.php        sign-in page
invoice.php      printable invoice
report.php       printable timesheets & reports, CSV export
backup.php       database download
config.php       your settings: email, mail server, data folder (you create it; never committed)
lib/             shared PHP code: passkeys, email, billing (not web-accessible)
data/            your database and session files (not web-accessible, not in git)
assets/          CSS and JavaScript
router.php, start.sh, start.bat   for running from a shared drive
```
