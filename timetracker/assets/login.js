// Record the browser's time zone on first setup so timers use local time.
try {
    document.getElementById('tz').value = Intl.DateTimeFormat().resolvedOptions().timeZone || '';
} catch (e) { /* leave the default */ }
