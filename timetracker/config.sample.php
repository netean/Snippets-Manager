<?php
// Copy this file to config.php and fill it in. config.php is never committed to git
// and is blocked from the web, so your email address and mail password stay private.
return [
    // Where sign-in codes are sent (backup sign-in and first-time setup). Never shown in the app.
    'owner_email' => 'you@example.com',

    // How emails are sent. Pick ONE of these:

    // 1. Your web host's mail() function (works on most shared hosting):
    'mail' => ['transport' => 'mail', 'from' => 'timesheets@yourdomain.com'],

    // 2. An SMTP server. For Gmail, turn on 2-Step Verification, create an
    //    App Password (myaccount.google.com/apppasswords) and use it below:
    // 'mail' => [
    //     'transport' => 'smtp',
    //     'host' => 'smtp.gmail.com',
    //     'port' => 587,
    //     'secure' => 'tls',            // 'tls' (port 587), 'ssl' (port 465) or 'none'
    //     'username' => 'you@gmail.com',
    //     'password' => 'your-16-character-app-password',
    //     'from' => 'you@gmail.com',
    // ],

    // 3. Testing only: write emails to data/mail.log instead of sending them.
    // 'mail' => ['transport' => 'log'],

    // Optional: keep your data outside the web root (recommended on a web host).
    // 'data_dir' => '/home/youraccount/timetracker-data',

    // Optional: how long you stay signed in on a device.
    // 'session_days' => 30,

    // Optional: only needed if passkeys complain about the address (e.g. behind a proxy).
    // 'rp_id' => 'time.yourdomain.com',
    // 'origin' => 'https://time.yourdomain.com',
];
