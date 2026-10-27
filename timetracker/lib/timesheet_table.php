<?php
// Detailed list of entries grouped by day. Expects $entries; $showAmounts is optional.
$showAmounts = $showAmounts ?? false;
$catNames = category_names();
$byDay = [];
foreach ($entries as $e) {
    $byDay[$e['date']][] = $e;
}
$multiClient = count(array_unique(array_column($entries, 'client_id'))) > 1;
$total = 0;
$totalAmount = 0.0;
$cols = $showAmounts ? 6 : 5;
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
    <?php foreach ($byDay as $date => $dayEntries): $dayTotal = 0; $dayAmount = 0.0; ?>
        <tbody>
        <?php foreach ($dayEntries as $i => $e): $dayTotal += $e['minutes']; $dayAmount += $e['amount']; ?>
            <tr>
                <td class="nowrap"><?= $i === 0 ? h(date('D', strtotime($date)) . ' ' . fmt_date($date)) : '' ?></td>
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
        <?php endforeach; $total += $dayTotal; $totalAmount += $dayAmount; ?>
        <tr class="day-total">
            <th colspan="4">Total for <?= h(fmt_date($date)) ?></th>
            <td class="num"><?= fmt_hours($dayTotal) ?></td>
            <?php if ($showAmounts): ?><td class="num"><?= h(fmt_money($dayAmount)) ?></td><?php endif ?>
        </tr>
        </tbody>
    <?php endforeach ?>
    <tfoot>
    <tr class="grand">
        <th colspan="4">Total</th>
        <td class="num"><?= fmt_hours($total) ?></td>
        <?php if ($showAmounts): ?><td class="num"><?= h(fmt_money($totalAmount)) ?></td><?php endif ?>
    </tr>
    </tfoot>
</table>
<?php if (!$entries): ?><p class="muted">No time recorded.</p><?php endif ?>
<?php if (array_filter($entries, fn($e) => !$e['billable'])): ?><p class="sub">(nb) = not billable</p><?php endif ?>
