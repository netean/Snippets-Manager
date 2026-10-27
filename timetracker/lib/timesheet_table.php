<?php
// Detailed list of entries grouped by day, written out as the entries are read
// (so long reports don't need much memory).
// Expects: $entries (iterable, ordered by date). Optional: $showAmounts, $multiClient.
$showAmounts = $showAmounts ?? false;
$multiClient = $multiClient ?? false;
$catNames = category_names();
$total = 0;
$totalAmount = 0.0;
$rows = 0;
$hasNonBillable = false;
$day = null; // ['date', 'minutes', 'amount', 'billed' => [client => units]]

$closeDay = function (?array $day) use ($showAmounts, $multiClient): void {
    if ($day === null) {
        return;
    }
    $billed = $day['billed'] ? ' <span class="sub">— billed as ' . h(implode(', ', array_map(
        fn($c, $u) => fmt_days($u) . ($multiClient ? " ($c)" : ''), array_keys($day['billed']), $day['billed']))) . '</span>' : '';
    echo '<tr class="day-total"><th colspan="4">Total for ', h(fmt_date($day['date'])), $billed, '</th>',
        '<td class="num">', fmt_hours($day['minutes']), '</td>',
        $showAmounts ? '<td class="num">' . h(fmt_money($day['amount'])) . '</td>' : '', "</tr></tbody>\n";
};
?>
<table class="lines timesheet">
    <thead>
    <tr>
        <th>Date</th>
        <th>Project / task</th>
        <th>Description</th>
        <th>Time</th>
        <th class="num">Hours</th>
        <?php if ($showAmounts): ?><th class="num">Amount</th><?php endif ?>
    </tr>
    </thead>
    <?php foreach ($entries as $e):
        $rows++;
        $first = $day === null || $day['date'] !== $e['date'];
        if ($first) {
            $closeDay($day);
            $day = ['date' => $e['date'], 'minutes' => 0, 'amount' => 0.0, 'billed' => []];
            echo '<tbody>';
        }
        $day['minutes'] += $e['minutes'];
        $day['amount'] += $e['amount'];
        $total += $e['minutes'];
        $totalAmount += $e['amount'];
        if ($e['bill_units'] !== null) {
            $day['billed'][$e['client_name']] = $e['bill_units'];
        }
        $hasNonBillable = $hasNonBillable || !$e['billable'];
        ?>
        <tr>
            <td class="nowrap"><?= $first ? h(date('D', strtotime($e['date'])) . ' ' . fmt_date($e['date'])) : '' ?></td>
            <td>
                <?= $multiClient ? h($e['client_name']) . ' — ' : '' ?><?= h($e['project_name']) ?>
                <?php if ($e['task_name']): ?><div class="sub"><?= h($e['task_name']) ?></div><?php endif ?>
            </td>
            <td>
                <?= h($e['description']) ?>
                <?php if ($e['category_ids']): ?>
                    <div class="sub"><?= h(implode(', ', array_map(fn($c) => $catNames[$c] ?? '', $e['category_ids']))) ?></div>
                <?php endif ?>
            </td>
            <td class="nowrap"><?= $e['start_time'] ? h($e['start_time'] . '–' . $e['end_time']) : '' ?></td>
            <td class="num"><?= fmt_hours($e['minutes']) ?><?= $e['billable'] ? '' : ' <span class="sub">(nb)</span>' ?></td>
            <?php if ($showAmounts): ?><td class="num"><?= $e['billable'] ? h(fmt_money($e['amount'])) : '' ?></td><?php endif ?>
        </tr>
    <?php endforeach; $closeDay($day); ?>
    <tfoot>
    <tr class="grand">
        <th colspan="4">Total</th>
        <td class="num"><?= fmt_hours($total) ?></td>
        <?php if ($showAmounts): ?><td class="num"><?= h(fmt_money($totalAmount)) ?></td><?php endif ?>
    </tr>
    </tfoot>
</table>
<?php if (!$rows): ?><p class="muted">No time recorded.</p><?php endif ?>
<?php if ($hasNonBillable): ?><p class="sub">(nb) = not billable</p><?php endif ?>
