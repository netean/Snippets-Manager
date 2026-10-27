<?php
// Printable invoice. Use the browser's "Print → Save as PDF" to send it.
declare(strict_types=1);
require __DIR__ . '/lib/app.php';

start_session();
send_security_headers();
require_login();

$id = (int) ($_GET['id'] ?? 0);
$s = db()->prepare('SELECT i.*, c.name AS client_name, c.contact, c.email AS client_email, c.address AS client_address, c.invoice_notes
    FROM invoices i LEFT JOIN clients c ON c.id = i.client_id WHERE i.id = ?');
$s->execute([$id]);
$inv = $s->fetch();
if (!$inv) {
    http_response_code(404);
    exit('Invoice not found.');
}
$s = db()->prepare('SELECT * FROM invoice_lines WHERE invoice_id = ? ORDER BY sort, id');
$s->execute([$id]);
$lines = $s->fetchAll();
$withTimesheet = !empty($_GET['timesheet']);
$entries = $withTimesheet ? fetch_entries(['invoice_id' => $id]) : [];
$set = settings();

$billTo = $inv['bill_to'];
if ($billTo === null || $billTo === '') {
    $billTo = $inv['client_id']
        ? trim(implode("\n", array_filter([$inv['client_name'], $inv['contact'] ? 'Attn: ' . $inv['contact'] : null, $inv['client_address']])))
        : (string) $set['umbrella_bill_to'];
}
$from = trim(implode("\n", array_filter([$set['your_name'] !== $set['business_name'] ? $set['your_name'] : null, $set['address']])));
$taxLabel = $set['tax_label'] ?: 'Tax';
$qtyFmt = fn($q) => rtrim(rtrim(number_format((float) $q, 2), '0'), '.');
?><!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Invoice <?= h($inv['number']) ?><?= $inv['client_name'] ? ' — ' . h($inv['client_name']) : '' ?></title>
<link rel="stylesheet" href="assets/print.css?v=<?= APP_VERSION ?>">
</head>
<body>
<div class="toolbar no-print">
    <button type="button" data-print>Print / Save as PDF</button>
    <?php if ($withTimesheet): ?>
        <a href="?id=<?= $id ?>">Hide timesheet</a>
    <?php else: ?>
        <a href="?id=<?= $id ?>&amp;timesheet=1">Attach timesheet</a>
    <?php endif ?>
    <a href="./#invoices">Back to invoices</a>
</div>

<article class="page">
    <header class="doc-head">
        <div>
            <h1 class="biz"><?= h($set['business_name'] ?: $set['your_name'] ?: 'Your business name') ?></h1>
            <div class="pre"><?= h($from) ?></div>
            <div><?= h(implode(' · ', array_filter([$set['email'], $set['phone'], $set['website']]))) ?></div>
        </div>
        <div class="doc-title">
            <h2>Invoice</h2>
            <table class="meta">
                <tr><th>Invoice no.</th><td><?= h($inv['number']) ?></td></tr>
                <tr><th>Date</th><td><?= h(fmt_date($inv['issue_date'])) ?></td></tr>
                <?php if ($inv['due_date']): ?><tr><th>Due</th><td><?= h(fmt_date($inv['due_date'])) ?></td></tr><?php endif ?>
                <?php if ($inv['period_from'] || $inv['period_to']): ?>
                    <tr><th>Period</th><td><?= h(describe_period($inv['period_from'], $inv['period_to'])) ?></td></tr>
                <?php endif ?>
                <?php if ($set['tax_number']): ?><tr><th><?= h($taxLabel) ?> no.</th><td><?= h($set['tax_number']) ?></td></tr><?php endif ?>
            </table>
        </div>
    </header>

    <section class="bill-to">
        <h3>Bill to</h3>
        <div class="pre"><?= h($billTo) ?></div>
    </section>

    <table class="lines">
        <thead><tr><th>Description</th><th class="num">Qty</th><th class="num">Rate</th><th class="num">Amount</th></tr></thead>
        <tbody>
        <?php foreach ($lines as $l): ?>
            <tr>
                <td><?= h($l['description']) ?></td>
                <td class="num"><?= h(fmt_qty((float) $l['quantity'], $l['unit'])) ?></td>
                <td class="num"><?= h(fmt_money((float) $l['unit_price'])) ?></td>
                <td class="num"><?= h(fmt_money((float) $l['amount'])) ?></td>
            </tr>
        <?php endforeach ?>
        </tbody>
        <tfoot>
            <tr><th colspan="3">Subtotal</th><td class="num"><?= h(fmt_money((float) $inv['subtotal'])) ?></td></tr>
            <?php if ((float) $inv['tax_rate'] != 0.0): ?>
                <tr><th colspan="3"><?= h($taxLabel) ?> @ <?= h($qtyFmt($inv['tax_rate'])) ?>%</th><td class="num"><?= h(fmt_money((float) $inv['tax'])) ?></td></tr>
            <?php endif ?>
            <tr class="grand"><th colspan="3">Total due</th><td class="num"><?= h(fmt_money((float) $inv['total'])) ?></td></tr>
        </tfoot>
    </table>

    <?php if ($inv['status'] === 'paid'): ?>
        <p class="paid-stamp">Paid<?= $inv['paid_date'] ? ' ' . h(fmt_date($inv['paid_date'])) : '' ?></p>
    <?php endif ?>

    <?php if ($set['bank_details'] || $inv['due_date'] || $set['payment_terms']): ?>
        <section class="payment">
            <h3>Payment</h3>
            <?php if ($inv['due_date']): ?><p>Please pay by <strong><?= h(fmt_date($inv['due_date'])) ?></strong>, quoting <?= h($inv['number']) ?>.</p><?php endif ?>
            <div class="pre"><?= h($set['bank_details']) ?></div>
        </section>
    <?php endif ?>

    <?php if ($inv['notes'] || $inv['invoice_notes']): ?>
        <section class="notes pre"><?= h(trim($inv['notes'] . "\n" . $inv['invoice_notes'])) ?></section>
    <?php endif ?>

    <footer class="doc-foot">
        <?= h($set['invoice_footer']) ?>
        <?php if ($set['company_number']): ?><br>Company no. <?= h($set['company_number']) ?><?php endif ?>
    </footer>
</article>

<?php if ($withTimesheet): ?>
    <article class="page break-before">
        <h2>Timesheet for invoice <?= h($inv['number']) ?></h2>
        <p class="muted"><?= h(describe_period($inv['period_from'], $inv['period_to'])) ?></p>
        <?php require __DIR__ . '/lib/timesheet_table.php'; ?>
    </article>
<?php endif ?>
<script src="assets/print.js"></script>
</body>
</html>
