<?php
// Copy this file to config.php and fill it in (deploy/install-almalinux.sh does this for you).
// config.php is never committed to git, blocked from the web, and not included in
// database backups — so your sign-in address and mail keys stay private.
return [
    // Where sign-in codes and security alerts are sent. Never shown in the app.
    'owner_email' => 'you@example.com',

    // How emails are sent. Pick ONE of these blocks.

    // 1. Brevo transactional API (recommended if you use Brevo).
    //    Brevo → SMTP & API → API keys → "Generate a new API key".
    //    'from' must be a sender or domain you have verified in Brevo.
    'mail' => [
        'transport' => 'brevo',
        'api_key' => 'xkeysib-…',
        'from' => 'noreply@yourdomain.com',
        // 'from_name' => 'Timesheets',   // defaults to your company name from Settings
    ],

    // 2. Brevo SMTP relay. Brevo → SMTP & API → SMTP: use the "Login" shown there
    //    (looks like 1234ab@smtp-brevo.com) and an SMTP key as the password.
    // 'mail' => [
    //     'transport' => 'smtp',
    //     'host' => 'smtp-relay.brevo.com',
    //     'port' => 587,
    //     'secure' => 'tls',            // 'tls' (port 587), 'ssl' (port 465) or 'none'
    //     'username' => '1234ab@smtp-brevo.com',
    //     'password' => 'xsmtpsib-…',
    //     'from' => 'noreply@yourdomain.com',
    // ],

    // 3. Any other SMTP server, e.g. Gmail with an App Password: as block 2 with
    //    host smtp.gmail.com and your Gmail address as username and from.

    // 4. PHP's mail() on this server: ['transport' => 'mail', 'from' => 'timesheets@yourdomain.com']
    // 5. Testing only — write emails to data/mail.log: ['transport' => 'log']

    // Where the database lives. Keep it outside the web root.
    // 'data_dir' => '/var/lib/timetracker',

    // Optional: how long you stay signed in on a device.
    // 'session_days' => 30,

    // Optional: only needed if passkeys complain about the address (e.g. behind a proxy).
    // 'rp_id' => 'time.yourdomain.com',
    // 'origin' => 'https://time.yourdomain.com',
];
