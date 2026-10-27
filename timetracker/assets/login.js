'use strict';

(() => {
    const csrf = document.querySelector('meta[name="csrf-token"]').content;
    const mode = document.body.dataset.mode;
    const hasPasskeys = document.body.dataset.passkeys === '1';
    const errorBox = document.getElementById('error');
    const noticeBox = document.getElementById('notice');

    async function call(action, data = {}) {
        const r = await fetch('auth.php?action=' + action, {
            method: 'POST', credentials: 'same-origin',
            headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': csrf },
            body: JSON.stringify(data),
        });
        const j = await r.json().catch(() => ({ error: 'Unexpected response from the server.' }));
        if (!r.ok || j.error) throw new Error(j.error || 'HTTP ' + r.status);
        return j;
    }

    const show = (step) => {
        document.querySelectorAll('[data-step]').forEach((s) => { s.hidden = s.dataset.step !== step; });
        const first = document.querySelector(`[data-step="${step}"] input`);
        if (first) first.focus();
    };
    const error = (msg) => { errorBox.textContent = msg || ''; errorBox.hidden = !msg; if (msg) noticeBox.hidden = true; };
    const notice = (msg) => { noticeBox.textContent = msg || ''; noticeBox.hidden = !msg; };

    // Disables the button while working and shows any error.
    async function busy(btn, fn) {
        error('');
        btn.disabled = true;
        try { await fn(); } catch (e) { error(e.message); } finally { btn.disabled = false; }
    }
    const done = () => { location.href = mode === 'setup' ? './#settings' : './'; };

    document.querySelectorAll('[data-goto]').forEach((b) => b.addEventListener('click', () => { error(''); notice(''); show(b.dataset.goto); }));
    if (!hasPasskeys) document.querySelectorAll('[data-needs-passkey]').forEach((b) => { b.hidden = true; });

    if (mode === 'setup') {
        const sendBtn = document.getElementById('setup-send');
        sendBtn.addEventListener('click', () => busy(sendBtn, async () => {
            await call('setup.start');
            notice('Code sent — check your email.');
            show('setup-finish');
        }));
        const form = document.querySelector('[data-step="setup-finish"]');
        form.addEventListener('submit', (ev) => {
            ev.preventDefault();
            busy(form.querySelector('button[type=submit]'), async () => {
                let tz = '';
                try { tz = Intl.DateTimeFormat().resolvedOptions().timeZone || ''; } catch (e) { /* keep default */ }
                await call('setup.finish', { code: form.code.value, password: form.password.value, confirm: form.confirm.value, tz });
                done();
            });
        });
        return;
    }

    const pkBtn = document.getElementById('passkey-login');
    pkBtn.addEventListener('click', () => busy(pkBtn, async () => {
        if (!Passkey.supported()) throw new Error('This browser can’t use passkeys here. Use your password and an email code instead.');
        const options = await call('passkey.options');
        const assertion = await Passkey.get(options);
        await call('passkey.verify', assertion);
        done();
    }));

    const pwForm = document.querySelector('[data-step="backup-password"]');
    pwForm.addEventListener('submit', (ev) => {
        ev.preventDefault();
        busy(pwForm.querySelector('button[type=submit]'), async () => {
            await call('backup.start', { password: pwForm.password.value });
            pwForm.password.value = '';
            show('backup-code');
        });
    });
    const codeForm = document.querySelector('[data-step="backup-code"]');
    codeForm.addEventListener('submit', (ev) => {
        ev.preventDefault();
        busy(codeForm.querySelector('button[type=submit]'), async () => {
            await call('backup.verify', { code: codeForm.code.value });
            done();
        });
    });

    if (!hasPasskeys || !Passkey.supported()) {
        show('backup-password');
        if (hasPasskeys) notice('This browser can’t use passkeys here, so sign in with your password and an email code.');
    }
})();
