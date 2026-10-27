'use strict';

// ---------------------------------------------------------------------------
// State and server access

const S = {
    csrf: document.querySelector('meta[name="csrf-token"]').content,
    settings: {}, clients: [], projects: [], tasks: [], categories: [],
    timer: null, clockOffset: 0,
    track: { weekStart: null, selected: new Set(), mode: 'timer', q: '' },
    ts: { weekStart: null, extraRows: [] },
    rep: { preset: 'this_month', from: '', to: '', client_id: '', project_id: '', category_id: '', billable: '' },
    inv: null,
    showArchived: false,
};

async function api(action, data = {}) {
    const r = await fetch('api.php?action=' + encodeURIComponent(action), {
        method: 'POST',
        credentials: 'same-origin',
        headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': S.csrf },
        body: JSON.stringify(data),
    });
    if (r.status === 401) {
        location.href = 'login.php';
        throw new Error('Signed out');
    }
    const j = await r.json().catch(() => ({ error: 'Unexpected response from the server (HTTP ' + r.status + ')' }));
    if (!r.ok || j.error) throw new Error(j.error || 'HTTP ' + r.status);
    return j;
}

async function loadBootstrap() {
    const b = await api('bootstrap');
    Object.assign(S, {
        settings: b.settings, clients: b.clients, projects: b.projects, tasks: b.tasks, categories: b.categories, timer: b.timer,
    });
    S.clockOffset = b.server_now - Date.now();
    for (const list of [S.clients, S.projects, S.tasks, S.categories]) {
        for (const r of list) {
            r.id = +r.id;
            r.archived = +r.archived;
            if ('client_id' in r) r.client_id = +r.client_id;
            if ('project_id' in r) r.project_id = r.project_id == null ? null : +r.project_id;
            if ('billable' in r) r.billable = +r.billable;
        }
    }
}

// ---------------------------------------------------------------------------
// Small DOM helpers

function el(tag, attrs, ...kids) {
    const e = document.createElement(tag);
    const late = {};
    for (const [k, v] of Object.entries(attrs || {})) {
        if (v == null || v === false) continue;
        if (k === 'class') e.className = v;
        else if (k === 'style' && typeof v === 'object') {
            for (const [sk, sv] of Object.entries(v)) if (sv != null) e.style.setProperty(sk.startsWith('--') ? sk : sk.replace(/[A-Z]/g, (c) => '-' + c.toLowerCase()), sv);
        }
        else if (k.startsWith('on') && typeof v === 'function') e.addEventListener(k.slice(2), v);
        else if (k === 'value' || k === 'checked' || k === 'selected') late[k] = v;
        else if (k === 'dataset') Object.assign(e.dataset, v);
        else e.setAttribute(k, v === true ? '' : v);
    }
    for (const kid of kids.flat(Infinity)) {
        if (kid == null || kid === false) continue;
        e.append(kid instanceof Node ? kid : String(kid));
    }
    Object.assign(e, late);
    return e;
}

const $ = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];

// Like replaceChildren, but skips null/false and flattens arrays (for conditional content).
function fill(node, ...kids) {
    node.replaceChildren(...kids.flat(Infinity).filter((k) => k != null && k !== false));
}

function setView(node) {
    const v = $('#view');
    v.replaceChildren(node);
}

let toastTimer;
function toast(msg, isError = false) {
    const t = $('#toast');
    t.textContent = msg;
    t.className = 'toast' + (isError ? ' error' : '');
    t.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { t.hidden = true; }, isError ? 6000 : 2500);
}

// Runs an async action and shows any error as a toast.
async function attempt(fn) {
    try {
        return await fn();
    } catch (e) {
        toast(e.message, true);
        return undefined;
    }
}

function debounce(fn, ms) {
    let t;
    return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); };
}

// ---------------------------------------------------------------------------
// Dates, durations and money

const pad = (n) => String(n).padStart(2, '0');
const ymd = (d) => `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
const parseYmd = (s) => { const [y, m, d] = s.split('-').map(Number); return new Date(y, m - 1, d); };
const addDays = (s, n) => { const d = parseYmd(s); d.setDate(d.getDate() + n); return ymd(d); };
const today = () => ymd(new Date());
const dayName = (s, style = 'short') => parseYmd(s).toLocaleDateString(undefined, { weekday: style });
const monthStart = (s) => s.slice(0, 8) + '01';
const monthEnd = (s) => { const d = parseYmd(s); return ymd(new Date(d.getFullYear(), d.getMonth() + 1, 0)); };

function weekStart(s) {
    const d = parseYmd(s);
    const ws = +S.settings.week_start || 1; // ISO: 1 = Monday … 7 = Sunday
    const dow = d.getDay() || 7;
    d.setDate(d.getDate() - ((dow - ws + 7) % 7));
    return ymd(d);
}

function fmtDate(s) {
    if (!s) return '';
    const [y, m, d] = s.split('-');
    return (S.settings.date_format || 'd/m/Y').replace(/[dmYy]/g, (t) => ({ d, m, Y: y, y: y.slice(2) })[t]);
}

function fmtRange(from, to) {
    const f = parseYmd(from), t = parseYmd(to);
    const o = { day: 'numeric', month: 'short' };
    return f.toLocaleDateString(undefined, o) + ' – ' + t.toLocaleDateString(undefined, { ...o, year: 'numeric' });
}

const fmtQty = (q, unit) => unit === 'days'
    ? (q === 0.5 ? '½ day' : `${+q.toFixed(2)} ${q === 1 ? 'day' : 'days'}`)
    : `${(+q).toFixed(2)}${unit ? ' ' + unit : ''}`;
const fmtDur = (min) => `${Math.floor(min / 60)}:${pad(Math.round(min % 60))}`;
const fmtHrs = (min) => (min / 60).toFixed(2);

// Accepts 1:30, 1.5, 1,5, 1h30, 1h 30m, 90m, 2h. A bare number up to 24 is hours, above that minutes.
function parseDuration(str) {
    const s = String(str ?? '').trim().toLowerCase().replace(',', '.');
    if (!s) return null;
    let m = s.match(/^(\d+):([0-5]?\d)$/);
    if (m) return +m[1] * 60 + +m[2];
    if (/^\d+(?:\.\d+)?$/.test(s)) {
        const n = +s;
        return n <= 24 ? Math.round(n * 60) : Math.round(n);
    }
    m = s.match(/^(\d+(?:\.\d+)?)\s*h(?:ours?|rs?)?\s*(?:(\d+)\s*(?:m(?:ins?|inutes?)?)?)?$/) // 2h, 1h30, 1h 30m
        || s.match(/^()(\d+)\s*m(?:ins?|inutes?)?$/); // 90m
    if (m) return Math.round((+m[1] || 0) * 60 + (+m[2] || 0));
    return null;
}

// Accepts 9, 930, 09:30, 9.30, 2pm, 2:30pm. Returns "HH:MM", '' for empty, or null if invalid.
function parseTime(str) {
    const s = String(str ?? '').trim().toLowerCase();
    if (!s) return '';
    const m = s.match(/^(\d{1,2})(?:[:.]?(\d{2}))?\s*(am|pm|a|p)?$/);
    if (!m) return null;
    let h = +m[1];
    const mi = +(m[2] || 0);
    if (m[3]) {
        if (h > 12 || h === 0) return null;
        if (m[3].startsWith('p') && h < 12) h += 12;
        if (m[3].startsWith('a') && h === 12) h = 0;
    }
    if (h > 23 || mi > 59) return null;
    return `${pad(h)}:${pad(mi)}`;
}

const timeToMin = (t) => { const [h, m] = t.split(':').map(Number); return h * 60 + m; };
const minToTime = (m) => { m = ((m % 1440) + 1440) % 1440; return `${pad(Math.floor(m / 60))}:${pad(m % 60)}`; };

function money(n) {
    try {
        return new Intl.NumberFormat(undefined, { style: 'currency', currency: S.settings.currency_code || 'GBP' }).format(n);
    } catch (e) {
        return (S.settings.currency_symbol || '') + Number(n).toFixed(2);
    }
}

function periodRange(preset, base = today()) {
    const ws = weekStart(base);
    const y = base.slice(0, 4);
    switch (preset) {
        case 'this_week': return [ws, addDays(ws, 6)];
        case 'last_week': return [addDays(ws, -7), addDays(ws, -1)];
        case 'this_month': return [monthStart(base), monthEnd(base)];
        case 'last_month': { const p = addDays(monthStart(base), -1); return [monthStart(p), monthEnd(p)]; }
        case 'this_year': return [`${y}-01-01`, `${y}-12-31`];
        case 'last_year': return [`${y - 1}-01-01`, `${y - 1}-12-31`];
        case 'all': return ['', ''];
        default: return null;
    }
}

const PERIODS = [
    ['this_week', 'This week'], ['last_week', 'Last week'], ['this_month', 'This month'], ['last_month', 'Last month'],
    ['this_year', 'This year'], ['last_year', 'Last year'], ['all', 'All time'], ['custom', 'Custom dates'],
];

// Moves a period one step back or forward, keeping its shape (week, month, year or custom length).
function shiftPeriod(from, to, dir) {
    if (!from || !to) return [from, to];
    if (from === monthStart(from) && to === monthEnd(from)) {
        const d = parseYmd(from);
        const n = ymd(new Date(d.getFullYear(), d.getMonth() + dir, 1));
        return [n, monthEnd(n)];
    }
    const len = Math.round((parseYmd(to) - parseYmd(from)) / 86400000) + 1;
    return [addDays(from, dir * len), addDays(to, dir * len)];
}

// ---------------------------------------------------------------------------
// Lookups and pickers

const byId = (list, id) => list.find((r) => r.id === +id);
const clientOf = (p) => byId(S.clients, p.client_id);

function projectLabel(pid) {
    const p = byId(S.projects, pid);
    return p ? p.name : '?';
}

function tasksFor(projectId) {
    return S.tasks.filter((t) => t.project_id === null || t.project_id === +projectId);
}

// <select> of projects grouped by client. Archived ones are hidden unless already chosen.
function projectSelect(selected, { blank = 'Choose project…', allowNew = true, onchange } = {}) {
    const sel = el('select', { class: 'project-select', onchange: (ev) => {
        if (sel.value === '__new') {
            sel.value = sel.dataset.prev || '';
            editRecord('projects', null, async (id) => {
                sel.replaceWith(projectSelect(id, { blank, allowNew, onchange }));
                onchange && onchange(id);
            });
            return;
        }
        sel.dataset.prev = sel.value;
        onchange && onchange(sel.value ? +sel.value : null, ev);
    } });
    sel.append(el('option', { value: '' }, blank));
    for (const c of S.clients) {
        const ps = S.projects.filter((p) => p.client_id === c.id && (!p.archived && !c.archived || p.id === +selected));
        if (!ps.length) continue;
        sel.append(el('optgroup', { label: c.name }, ps.map((p) => el('option', { value: p.id }, p.name + (p.code ? ` (${p.code})` : '')))));
    }
    if (allowNew) sel.append(el('option', { value: '__new' }, '＋ New project…'));
    sel.value = selected ? String(selected) : '';
    sel.dataset.prev = sel.value;
    return sel;
}

function fillTaskSelect(sel, projectId, selected) {
    fill(sel, el('option', { value: '' }, projectId ? 'No task' : 'Task'));
    if (projectId) {
        for (const t of tasksFor(projectId)) {
            if (t.archived && t.id !== +selected) continue;
            sel.append(el('option', { value: t.id }, t.name));
        }
    }
    sel.value = selected && tasksFor(projectId).some((t) => t.id === +selected) ? String(selected) : '';
    sel.disabled = !projectId;
}

function clientSelect(selected, blank = 'All clients', onchange) {
    const sel = el('select', { onchange: () => onchange && onchange(sel.value) }, el('option', { value: '' }, blank),
        S.clients.filter((c) => !c.archived || c.id === +selected).map((c) => el('option', { value: c.id }, c.name)));
    sel.value = selected ? String(selected) : '';
    return sel;
}

function categorySelect(selected, blank = 'All categories', onchange) {
    const sel = el('select', { onchange: () => onchange && onchange(sel.value) }, el('option', { value: '' }, blank),
        S.categories.filter((c) => !c.archived || c.id === +selected).map((c) => el('option', { value: c.id }, c.name)));
    sel.value = selected ? String(selected) : '';
    return sel;
}

// Multi-select for categories, with inline creation of new ones.
function categoryPicker(selectedIds = [], onchange) {
    let selected = new Set(selectedIds.map(Number));
    const wrap = el('div', { class: 'cat-picker' });
    const btn = el('button', { type: 'button', class: 'btn ghost cat-btn', title: 'Categories' });
    const panel = el('div', { class: 'cat-panel', hidden: true });
    const renderBtn = () => {
        const chosen = S.categories.filter((c) => selected.has(c.id));
        fill(btn, ...(chosen.length ? chosen.map(catChip) : [el('span', { class: 'muted' }, '🏷 Categories')]));
    };
    const renderPanel = () => {
        const newInput = el('input', { type: 'text', placeholder: 'New category…', onkeydown: (ev) => {
            if (ev.key === 'Enter') { ev.preventDefault(); add(); }
        } });
        const add = () => attempt(async () => {
            const name = newInput.value.trim();
            if (!name) return;
            const r = await api('categories.save', { name, color: randomColor() });
            await loadBootstrap();
            selected.add(r.id);
            renderPanel(); renderBtn(); onchange && onchange([...selected]);
        });
        fill(panel, 
            ...S.categories.filter((c) => !c.archived || selected.has(c.id)).map((c) => el('label', { class: 'check' },
                el('input', { type: 'checkbox', checked: selected.has(c.id), onchange: (ev) => {
                    ev.target.checked ? selected.add(c.id) : selected.delete(c.id);
                    renderBtn(); onchange && onchange([...selected]);
                } }), catChip(c))),
            el('div', { class: 'row' }, newInput, el('button', { type: 'button', class: 'btn small', onclick: add }, 'Add')),
        );
    };
    btn.addEventListener('click', () => {
        panel.hidden = !panel.hidden;
        if (!panel.hidden) renderPanel();
    });
    document.addEventListener('mousedown', (ev) => { if (!wrap.contains(ev.target)) panel.hidden = true; });
    wrap.append(btn, panel);
    renderBtn();
    wrap.getValue = () => [...selected];
    wrap.setValue = (ids) => { selected = new Set(ids.map(Number)); renderBtn(); };
    return wrap;
}

function catChip(c) {
    return el('span', { class: 'chip', style: { '--chip': c.color || '#64748b' } }, c.name);
}

function projectChip(pid) {
    const p = byId(S.projects, pid);
    if (!p) return el('span', { class: 'muted' }, '?');
    const c = clientOf(p);
    return el('span', { class: 'project-chip', style: { '--chip': p.color || '#2563eb' } },
        el('span', { class: 'dot' }), p.name, c ? el('span', { class: 'client' }, ' · ' + c.name) : null);
}

function randomColor() {
    const colors = ['#2563eb', '#16a34a', '#db2777', '#ea580c', '#7c3aed', '#0891b2', '#ca8a04', '#dc2626', '#4f46e5', '#059669'];
    return colors[Math.floor(Math.random() * colors.length)];
}

// ---------------------------------------------------------------------------
// Modal dialogs

function openModal({ title, body, actions, wide = false }) {
    const dlg = el('dialog', { class: 'modal' + (wide ? ' wide' : '') });
    const err = el('p', { class: 'error', hidden: true });
    const close = () => { if (dlg.open) dlg.close(); dlg.remove(); };
    const buttons = actions.map((a) => el('button', {
        type: a.submit ? 'submit' : 'button',
        class: 'btn ' + (a.class || ''),
        onclick: async (ev) => {
            ev.preventDefault();
            if (!a.run) return close();
            err.hidden = true;
            ev.currentTarget.disabled = true;
            const b = ev.currentTarget;
            try {
                const r = await a.run();
                if (r !== false) close();
            } catch (e) {
                err.textContent = e.message;
                err.hidden = false;
            } finally {
                b.disabled = false;
            }
        },
    }, a.label));
    dlg.append(el('form', { method: 'dialog' }, el('h2', {}, title), body, err,
        el('div', { class: 'modal-actions' }, buttons)));
    dlg.addEventListener('close', () => dlg.remove());
    document.body.append(dlg);
    dlg.showModal();
    return { close, dlg };
}

function confirmBox(message, okLabel = 'Delete') {
    return new Promise((resolve) => {
        const m = openModal({
            title: 'Are you sure?',
            body: el('p', {}, message),
            actions: [
                { label: 'Cancel', run: () => resolve(false) },
                { label: okLabel, class: 'danger', submit: true, run: () => resolve(true) },
            ],
        });
        m.dlg.addEventListener('cancel', () => resolve(false));
    });
}

// Builds labelled inputs from field definitions; returns the container with a values() method.
function formFields(defs, values = {}) {
    const inputs = {};
    const box = el('div', { class: 'form-grid' });
    for (const d of defs) {
        if (d.heading) {
            box.append(el('h3', { class: 'full' }, d.heading));
            continue;
        }
        let input;
        const v = values[d.name] ?? d.default ?? '';
        if (d.type === 'textarea') input = el('textarea', { rows: d.rows || 3, placeholder: d.placeholder, value: v ?? '' });
        else if (d.type === 'select') {
            input = el('select', {}, d.options.map(([ov, ol]) => el('option', { value: ov }, ol)));
            input.value = String(v ?? '');
        } else if (d.type === 'checkbox') input = el('input', { type: 'checkbox', checked: !!+v });
        else if (d.type === 'element') input = d.element;
        else input = el('input', { type: d.type || 'text', step: d.step, min: d.min, placeholder: d.placeholder, list: d.list, value: v ?? '', required: d.required });
        inputs[d.name] = input;
        const label = d.type === 'checkbox'
            ? el('label', { class: 'check' + (d.full ? ' full' : '') }, input, d.label)
            : el('label', { class: d.full || d.type === 'textarea' ? 'full' : '' }, d.label, input, d.hint ? el('small', { class: 'muted' }, d.hint) : null);
        box.append(label);
    }
    box.values = () => {
        const out = {};
        for (const d of defs) {
            if (!d.name || d.type === 'element') continue;
            const i = inputs[d.name];
            out[d.name] = d.type === 'checkbox' ? (i.checked ? 1 : 0) : i.value;
        }
        return out;
    };
    box.inputs = inputs;
    return box;
}

// ---------------------------------------------------------------------------
// Timer

function timerElapsed() {
    return S.timer ? Math.max(0, Date.now() + S.clockOffset - S.timer.started_at) : 0;
}

function fmtClock(ms) {
    const s = Math.floor(ms / 1000);
    return `${Math.floor(s / 3600)}:${pad(Math.floor(s / 60) % 60)}:${pad(s % 60)}`;
}

function tick() {
    const ind = $('#running-indicator');
    if (S.timer) {
        const t = fmtClock(timerElapsed());
        ind.hidden = false;
        ind.textContent = '● ' + t + (S.timer.project_id ? ' · ' + projectLabel(S.timer.project_id) : '');
        const c = $('#timer-clock');
        if (c) c.textContent = t;
        document.title = t + ' · Timesheets';
    } else {
        ind.hidden = true;
        document.title = 'Timesheets';
    }
}
setInterval(tick, 1000);

// ---------------------------------------------------------------------------
// Track view: quick entry / timer plus the week's entries

function rememberLast(fields) {
    try { localStorage.setItem('tt_last', JSON.stringify(fields)); } catch (e) { /* ignore */ }
}
function recallLast() {
    try { return JSON.parse(localStorage.getItem('tt_last')) || {}; } catch (e) { return {}; }
}

function quickEntry(onSaved) {
    const running = S.timer;
    const last = recallLast();
    const init = running || { project_id: last.project_id || null, task_id: null, category_ids: [], billable: true, description: '' };
    if (!running && init.project_id) {
        const p = byId(S.projects, init.project_id);
        if (!p || p.archived) init.project_id = null;
        else init.billable = !!p.billable;
    }

    const desc = el('input', { type: 'text', class: 'desc', placeholder: 'What are you working on?', value: init.description || '' });
    const taskSel = el('select', { class: 'task-select' });
    const billable = el('input', { type: 'checkbox', checked: !!init.billable });
    let projectId = init.project_id;
    const projSel = projectSelect(projectId, { onchange: (id) => {
        projectId = id;
        fillTaskSelect(taskSel, id, null);
        const p = byId(S.projects, id);
        if (p) billable.checked = !!p.billable;
        changed();
    } });
    fillTaskSelect(taskSel, projectId, init.task_id);
    const cats = categoryPicker(init.category_ids || [], () => changed());

    const fields = () => ({
        description: desc.value.trim(), project_id: projectId, task_id: taskSel.value ? +taskSel.value : null,
        category_ids: cats.getValue(), billable: billable.checked ? 1 : 0,
    });
    // Keep the running timer's details in sync so other computers see the same thing.
    const syncTimer = debounce(() => { if (S.timer) attempt(async () => { S.timer = (await api('timer.update', fields())).timer; }); }, 600);
    const changed = () => syncTimer();
    desc.addEventListener('input', changed);
    taskSel.addEventListener('change', changed);
    billable.addEventListener('change', changed);

    const mode = running ? 'timer' : S.track.mode;
    const body = el('div', { class: 'quick-row' });
    const modeBtn = (m, label, title) => el('button', {
        type: 'button', class: 'btn ghost mode' + (mode === m ? ' active' : ''), title, disabled: !!running && m !== 'timer',
        onclick: () => { S.track.mode = m; rerender(); },
    }, label);

    const rerender = () => { box.replaceWith(quickEntry(onSaved)); };

    let actions;
    if (mode === 'timer') {
        const clock = el('span', { id: 'timer-clock', class: 'clock' }, fmtClock(timerElapsed()));
        if (running) {
            actions = [clock,
                el('button', { type: 'button', class: 'btn danger', onclick: () => attempt(async () => {
                    await api('timer.stop', fields());
                    S.timer = null; tick();
                    toast('Time saved');
                    rememberLast(fields());
                    rerender(); onSaved();
                }) }, 'Stop'),
                el('button', { type: 'button', class: 'btn ghost icon', title: 'Discard timer', onclick: async () => {
                    if (!await confirmBox('Throw away the running timer without saving it?', 'Discard')) return;
                    attempt(async () => { await api('timer.discard'); S.timer = null; tick(); rerender(); });
                } }, '✕')];
        } else {
            actions = [clock, el('button', { type: 'button', class: 'btn primary', onclick: () => attempt(async () => {
                S.timer = (await api('timer.start', fields())).timer;
                tick(); rerender();
            }) }, 'Start')];
        }
    } else {
        const date = el('input', { type: 'date', value: today() });
        const start = el('input', { type: 'text', class: 'time', placeholder: 'Start', title: 'Start time, e.g. 9:00' });
        const end = el('input', { type: 'text', class: 'time', placeholder: 'End', title: 'End time, e.g. 17:30' });
        const dur = el('input', { type: 'text', class: 'dur', placeholder: '0:00', title: 'Duration, e.g. 1:30, 1.5 or 90m' });
        linkTimes(start, end, dur);
        const add = () => attempt(async () => {
            const t = readTimes(start, end, dur);
            const f = fields();
            if (!f.project_id) throw new Error('Choose a project first.');
            await api('entries.save', { ...f, date: date.value, ...t, project_ids: [f.project_id] });
            rememberLast(f);
            toast('Entry added');
            desc.value = ''; start.value = ''; end.value = ''; dur.value = '';
            desc.focus();
            onSaved();
        });
        for (const i of [desc, date, start, end, dur]) i.addEventListener('keydown', (ev) => { if (ev.key === 'Enter') { ev.preventDefault(); add(); } });
        actions = [date, start, el('span', { class: 'muted' }, '–'), end, dur, el('button', { type: 'button', class: 'btn primary', onclick: add }, 'Add')];
    }

    body.append(desc, projSel, taskSel, cats,
        el('label', { class: 'billable-toggle', title: 'Billable' }, billable, el('span', {}, S.settings.currency_symbol || '$')),
        el('div', { class: 'quick-actions' }, actions),
        el('div', { class: 'mode-toggle' }, modeBtn('timer', '⏱', 'Timer'), modeBtn('manual', '✎', 'Enter time manually')));
    const box = el('section', { class: 'card quick' + (running ? ' running' : '') }, body);
    return box;
}

// Fill in the third of start/end/duration when two are known.
function linkTimes(start, end, dur) {
    const norm = (i) => { const t = parseTime(i.value); if (t) i.value = t; i.classList.toggle('invalid', t === null); return t; };
    start.addEventListener('blur', () => {
        const s = norm(start);
        const d = parseDuration(dur.value);
        if (s && !end.value && d) end.value = minToTime(timeToMin(s) + d);
        else if (s && parseTime(end.value)) dur.value = fmtDur(diffMin(s, parseTime(end.value)));
    });
    end.addEventListener('blur', () => {
        const e = norm(end);
        const s = parseTime(start.value);
        if (s && e) dur.value = fmtDur(diffMin(s, e));
    });
    dur.addEventListener('blur', () => {
        const d = parseDuration(dur.value);
        dur.classList.toggle('invalid', dur.value.trim() !== '' && d === null);
        if (d === null) return;
        dur.value = fmtDur(d);
        const s = parseTime(start.value);
        if (s) end.value = minToTime(timeToMin(s) + d);
    });
}

function diffMin(s, e) {
    let m = timeToMin(e) - timeToMin(s);
    if (m <= 0) m += 1440;
    return m;
}

function readTimes(start, end, dur) {
    const s = parseTime(start.value), e = parseTime(end.value);
    if (s === null) throw new Error('The start time doesn’t look right — try 9:30.');
    if (e === null) throw new Error('The end time doesn’t look right — try 17:00.');
    const d = parseDuration(dur.value);
    if (!(s && e) && !d) throw new Error('Enter a duration (e.g. 1:30, 1.5 or 90m) or a start and end time.');
    return { start_time: s || null, end_time: e || null, minutes: s && e ? diffMin(s, e) : d };
}

async function viewTrack() {
    if (!S.track.weekStart) S.track.weekStart = weekStart(today());
    const list = el('div', { class: 'entries' });
    const refresh = () => loadTrackList(list);
    const parts = [];
    if (!S.projects.length) {
        parts.push(el('section', { class: 'card hint' },
            el('strong', {}, 'Getting started: '), 'add a client and a project, then start logging time. ',
            el('a', { href: '#manage', class: 'btn small primary' }, 'Add clients & projects'), ' ',
            el('a', { href: '#settings', class: 'btn small' }, 'Fill in your business details')));
    }
    parts.push(quickEntry(refresh), list);
    setView(el('div', {}, parts));
    await refresh();
}

async function loadTrackList(host) {
    const v = S.track;
    const from = v.weekStart, to = addDays(v.weekStart, 6);
    const { entries } = await api('entries.list', { from, to, q: v.q });
    const total = entries.reduce((a, e) => a + e.minutes, 0);
    const billable = entries.reduce((a, e) => a + (e.billable ? e.minutes : 0), 0);
    const reload = () => loadTrackList(host);
    const ids = new Set(entries.map((e) => e.id));
    for (const id of [...v.selected]) if (!ids.has(id)) v.selected.delete(id);

    const search = el('input', { type: 'search', placeholder: 'Search…', value: v.q, oninput: debounce(() => { v.q = search.value; reload(); }, 300) });
    const nav = el('div', { class: 'toolbar' },
        el('div', { class: 'week-nav' },
            el('button', { class: 'btn ghost icon', title: 'Previous week', onclick: () => { v.weekStart = addDays(v.weekStart, -7); reload(); } }, '‹'),
            el('button', { class: 'btn ghost', onclick: () => { v.weekStart = weekStart(today()); reload(); } }, from === weekStart(today()) ? 'This week' : fmtRange(from, to)),
            el('button', { class: 'btn ghost icon', title: 'Next week', onclick: () => { v.weekStart = addDays(v.weekStart, 7); reload(); } }, '›'),
            el('input', { type: 'date', title: 'Jump to a week', value: from, onchange: (ev) => { if (ev.target.value) { v.weekStart = weekStart(ev.target.value); reload(); } } })),
        el('span', { class: 'week-total' }, 'Week total ', el('strong', {}, fmtDur(total)), el('span', { class: 'muted' }, ` (${fmtDur(billable)} billable)`)),
        el('div', { class: 'spacer' }),
        search,
        el('button', { class: 'btn', onclick: () => entryModal(null, { date: today() }, reload) }, '＋ Add past time'),
        el('button', { class: 'btn', onclick: () => bulkAddModal(reload) }, '⧉ Bulk add'),
    );

    const bulkBar = el('div', { class: 'bulk-bar', hidden: !v.selected.size },
        el('strong', {}, `${v.selected.size} selected`),
        el('button', { class: 'btn small', onclick: () => bulkEditModal([...v.selected], reload) }, 'Edit…'),
        el('button', { class: 'btn small', onclick: () => duplicateModal([...v.selected], reload) }, 'Copy to date…'),
        el('button', { class: 'btn small danger', onclick: () => deleteEntries([...v.selected], reload) }, 'Delete'),
        el('button', { class: 'btn small ghost', onclick: () => { v.selected.clear(); reload(); } }, 'Clear selection'));

    const days = new Map();
    for (const e of entries) {
        if (!days.has(e.date)) days.set(e.date, []);
        days.get(e.date).push(e);
    }
    const groups = [];
    for (const [date, list] of days) {
        const dayTotal = list.reduce((a, e) => a + e.minutes, 0);
        const allSel = list.every((e) => v.selected.has(e.id));
        groups.push(el('section', { class: 'card day' },
            el('header', {},
                el('input', { type: 'checkbox', title: 'Select the whole day', checked: allSel, onchange: (ev) => {
                    list.forEach((e) => ev.target.checked ? v.selected.add(e.id) : v.selected.delete(e.id));
                    reload();
                } }),
                el('span', { class: 'day-name' }, date === today() ? 'Today' : date === addDays(today(), -1) ? 'Yesterday' : dayName(date, 'long')),
                el('span', { class: 'muted' }, fmtDate(date)),
                el('div', { class: 'spacer' }),
                el('button', { class: 'btn ghost small', title: 'Add time on this day', onclick: () => entryModal(null, { date }, reload) }, '＋'),
                el('span', { class: 'day-total' }, fmtDur(dayTotal))),
            list.map((e) => entryRow(e, reload))));
    }
    if (!entries.length) {
        groups.push(el('p', { class: 'empty muted' }, v.q ? 'Nothing matches your search this week.' : 'No time logged this week yet.'));
    }
    fill(host, nav, bulkBar, ...groups);
}

function entryRow(e, reload) {
    const v = S.track;
    const locked = e.invoice_id !== null;
    return el('div', { class: 'entry' + (locked ? ' locked' : ''), onclick: (ev) => {
        if (ev.target.closest('button, input')) return;
        entryModal(e, null, reload);
    } },
        el('input', { type: 'checkbox', checked: v.selected.has(e.id), onchange: (ev) => {
            ev.target.checked ? v.selected.add(e.id) : v.selected.delete(e.id);
            reload();
        } }),
        el('div', { class: 'what' },
            el('div', { class: 'desc' }, e.description || el('span', { class: 'muted' }, e.task_name || '(no description)')),
            el('div', { class: 'meta' }, projectChip(e.project_id), e.task_name && e.description ? el('span', { class: 'task' }, e.task_name) : null,
                e.category_ids.map((id) => byId(S.categories, id)).filter(Boolean).map(catChip))),
        el('span', { class: 'flags' },
            el('span', { class: 'billable' + (e.billable ? ' on' : ''), title: !e.billable ? 'Not billable' : e.billing_unit === 'hour' ? `Billable at ${money(e.eff_rate)}/h`
                : `Billed by the ${e.billing_unit === 'day' ? 'day' : 'half day'} at ${money(e.eff_rate)}/day — this date counts as ${fmtQty(e.bill_units, 'days')}` }, S.settings.currency_symbol || '$'),
            locked ? el('span', { class: 'lock', title: 'On invoice ' + e.invoice_number }, '🔒') : null),
        el('span', { class: 'times muted' }, e.start_time ? `${e.start_time}–${e.end_time}` : ''),
        el('span', { class: 'dur' }, fmtDur(e.minutes)),
        el('span', { class: 'row-actions' },
            el('button', { class: 'btn ghost icon', title: 'Start a timer for this again', disabled: !!S.timer, onclick: () => attempt(async () => {
                S.timer = (await api('timer.start', { description: e.description, project_id: e.project_id, task_id: e.task_id, category_ids: e.category_ids, billable: e.billable })).timer;
                tick(); route();
            }) }, '▶'),
            el('button', { class: 'btn ghost icon', title: 'Copy to today', onclick: () => attempt(async () => {
                await api('entries.duplicate', { ids: [e.id], date: today() });
                toast('Copied to today'); reload();
            }) }, '⧉'),
            el('button', { class: 'btn ghost icon', title: locked ? 'Invoiced — cannot delete' : 'Delete', disabled: locked, onclick: () => deleteEntries([e.id], reload) }, '🗑')));
}

async function deleteEntries(ids, reload) {
    if (!await confirmBox(ids.length === 1 ? 'Delete this time entry?' : `Delete ${ids.length} time entries?`)) return;
    attempt(async () => {
        const r = await api('entries.delete', { ids });
        ids.forEach((id) => S.track.selected.delete(id));
        toast(`Deleted ${r.deleted}` + (r.skipped ? ` (${r.skipped} invoiced entries kept)` : ''));
        reload();
    });
}

// Add or edit one entry. New entries can be split across several projects.
function entryModal(entry, defaults, onSaved) {
    const e = entry || { date: today(), billable: 1, category_ids: [], project_id: recallLast().project_id || null, ...defaults };
    const locked = entry && entry.invoice_id !== null;
    const date = el('input', { type: 'date', value: e.date, required: true });
    const start = el('input', { type: 'text', class: 'time', placeholder: '9:00', value: e.start_time || '' });
    const end = el('input', { type: 'text', class: 'time', placeholder: '17:00', value: e.end_time || '' });
    const dur = el('input', { type: 'text', class: 'dur', placeholder: '1:30', value: entry ? fmtDur(e.minutes) : '' });
    linkTimes(start, end, dur);
    const desc = el('textarea', { rows: 2, placeholder: 'What did you work on?', value: e.description || '' });
    const taskSel = el('select');
    const billable = el('input', { type: 'checkbox', checked: !!+e.billable });
    let projectId = e.project_id;
    const projSel = projectSelect(projectId, { onchange: (id) => {
        projectId = id;
        fillTaskSelect(taskSel, id, null);
        const p = byId(S.projects, id);
        if (p) billable.checked = !!p.billable;
    } });
    const projWrap = el('span', { class: 'proj-wrap' }, projSel);
    fillTaskSelect(taskSel, projectId, e.task_id);
    const cats = categoryPicker(e.category_ids);
    const rate = el('input', { type: 'number', step: '0.01', min: '0', placeholder: 'Use project/client rate', value: e.rate ?? '' });

    // Several projects at once (new entries only)
    const multi = el('div', { class: 'multi-projects', hidden: true });
    const splitMode = el('select', {}, el('option', { value: 'split' }, 'Split the time evenly between them'),
        el('option', { value: 'copy' }, 'Log the full time against each'));
    const multiChecks = S.projects.filter((p) => !p.archived).map((p) => el('label', { class: 'check' },
        el('input', { type: 'checkbox', value: p.id }), projectChip(p.id)));
    multi.append(el('div', { class: 'check-list' }, multiChecks), splitMode);
    const multiLink = entry ? null : el('a', { href: '#', class: 'small-link', onclick: (ev) => {
        ev.preventDefault();
        multi.hidden = !multi.hidden;
        projWrap.hidden = !multi.hidden;
        if (!multi.hidden && projectId) multi.querySelector(`input[value="${projectId}"]`).checked = true;
        ev.target.textContent = multi.hidden ? 'Several projects…' : 'Just one project';
    } }, 'Several projects…');

    const body = el('div', { class: 'form-grid' },
        locked ? el('p', { class: 'full warn' }, `This entry is on invoice ${e.invoice_number} and can’t be changed. Delete the invoice to unlock it.`) : null,
        el('label', {}, 'Date', date),
        el('div', { class: 'times-row' }, el('label', {}, 'Start', start), el('label', {}, 'End', end), el('label', {}, 'Duration', dur)),
        el('label', { class: 'full' }, 'Description', desc),
        el('label', {}, el('span', {}, 'Project ', multiLink), projWrap),
        el('label', {}, 'Task', taskSel),
        multi,
        el('label', {}, 'Categories', cats),
        el('label', {}, 'Rate override (per hour)', rate),
        el('label', { class: 'check' }, billable, 'Billable'));

    const save = async () => {
        const t = readTimes(start, end, dur);
        let projectIds = [projectId].filter(Boolean);
        if (!multi.hidden) projectIds = multiChecks.map((l) => l.querySelector('input')).filter((i) => i.checked).map((i) => +i.value);
        if (!projectIds.length) throw new Error('Choose a project.');
        await api('entries.save', {
            id: entry ? entry.id : null, date: date.value, ...t, description: desc.value, project_ids: projectIds,
            task_id: taskSel.value || null, category_ids: cats.getValue(), billable: billable.checked ? 1 : 0, rate: rate.value,
            split_mode: splitMode.value,
        });
        rememberLast({ project_id: projectIds[0] });
        toast(entry ? 'Entry updated' : projectIds.length > 1 ? `Added to ${projectIds.length} projects` : 'Entry added');
        onSaved();
    };
    const actions = [{ label: 'Cancel' }];
    if (entry) {
        actions.unshift({ label: 'Duplicate', class: 'ghost', run: async () => {
            await api('entries.duplicate', { ids: [entry.id] }); toast('Duplicated'); onSaved();
        } });
        if (!locked) actions.unshift({ label: 'Delete', class: 'danger ghost', run: async () => {
            if (!await confirmBox('Delete this time entry?')) return false;
            await api('entries.delete', { ids: [entry.id] }); toast('Deleted'); onSaved();
        } });
    }
    if (!locked) actions.push({ label: entry ? 'Save' : 'Add entry', class: 'primary', submit: true, run: save });
    openModal({ title: entry ? 'Edit time entry' : 'Add time', body, actions, wide: true });
    if (!entry) setTimeout(() => (e.date === today() ? dur : date).focus(), 50);
}

const WEEKDAYS = [[1, 'Mon'], [2, 'Tue'], [3, 'Wed'], [4, 'Thu'], [5, 'Fri'], [6, 'Sat'], [7, 'Sun']];

// The same time on every chosen weekday in a date range.
function bulkAddModal(onSaved, defaults = {}) {
    const ws = weekStart(today());
    const from = el('input', { type: 'date', value: defaults.from || addDays(ws, -7) });
    const to = el('input', { type: 'date', value: defaults.to || addDays(ws, -3) });
    const days = WEEKDAYS.map(([n, l]) => el('label', { class: 'check' }, el('input', { type: 'checkbox', value: n, checked: n <= 5 }), l));
    const start = el('input', { type: 'text', class: 'time', placeholder: 'optional' });
    const end = el('input', { type: 'text', class: 'time', placeholder: 'optional' });
    const dur = el('input', { type: 'text', class: 'dur', placeholder: '7:30', value: '7:30' });
    linkTimes(start, end, dur);
    const taskSel = el('select');
    const billable = el('input', { type: 'checkbox', checked: true });
    let projectId = defaults.project_id || recallLast().project_id || null;
    const projSel = projectSelect(projectId, { onchange: (id) => {
        projectId = id; fillTaskSelect(taskSel, id, null);
        const p = byId(S.projects, id); if (p) billable.checked = !!p.billable;
    } });
    fillTaskSelect(taskSel, projectId, null);
    const cats = categoryPicker([]);
    const desc = el('input', { type: 'text', placeholder: 'Optional description for every entry' });
    const skip = el('input', { type: 'checkbox', checked: true });
    const preview = el('p', { class: 'muted full' });
    const countDays = () => {
        if (!from.value || !to.value || to.value < from.value) return 0;
        const chosen = days.map((l) => l.querySelector('input')).filter((i) => i.checked).map((i) => +i.value);
        let n = 0;
        for (let d = from.value; d <= to.value; d = addDays(d, 1)) if (chosen.includes(parseYmd(d).getDay() || 7)) n++;
        return n;
    };
    const updatePreview = () => {
        const n = countDays();
        const m = parseDuration(dur.value) || 0;
        preview.textContent = n ? `Creates up to ${n} entries — ${fmtDur(n * m)} in total.` : 'No matching days.';
    };
    [from, to, dur, ...days.map((l) => l.firstChild)].forEach((i) => i.addEventListener('change', updatePreview));
    updatePreview();
    openModal({
        title: 'Bulk add time',
        wide: true,
        body: el('div', { class: 'form-grid' },
            el('label', {}, 'From', from), el('label', {}, 'To', to),
            el('div', { class: 'full weekday-row' }, el('span', {}, 'On these days: '), days),
            el('div', { class: 'times-row full' }, el('label', {}, 'Hours each day', dur), el('label', {}, 'Start', start), el('label', {}, 'End', end)),
            el('label', {}, 'Project', projSel), el('label', {}, 'Task', taskSel),
            el('label', {}, 'Categories', cats), el('label', {}, 'Description', desc),
            el('label', { class: 'check' }, billable, 'Billable'),
            el('label', { class: 'check' }, skip, 'Skip days that already have time logged'),
            preview),
        actions: [{ label: 'Cancel' }, { label: 'Create entries', class: 'primary', submit: true, run: async () => {
            const t = readTimes(start, end, dur);
            if (!projectId) throw new Error('Choose a project.');
            const r = await api('entries.bulk_add', {
                from: from.value, to: to.value, ...t, project_id: projectId, task_id: taskSel.value || null,
                weekdays: days.map((l) => l.querySelector('input')).filter((i) => i.checked).map((i) => +i.value),
                category_ids: cats.getValue(), description: desc.value, billable: billable.checked ? 1 : 0, skip_existing: skip.checked ? 1 : 0,
            });
            toast(`Created ${r.created} entries` + (r.skipped ? `, skipped ${r.skipped} days` : ''));
            onSaved();
        } }],
    });
}

function bulkEditModal(ids, onSaved) {
    const projSel = projectSelect(null, { blank: '— keep project —', allowNew: false, onchange: (id) => fillTask(id) });
    const taskSel = el('select');
    const fillTask = (pid) => {
        fill(taskSel, el('option', { value: 'keep' }, '— keep task —'), el('option', { value: '' }, 'No task'));
        const list = pid ? tasksFor(pid) : S.tasks.filter((t) => t.project_id === null);
        list.filter((t) => !t.archived).forEach((t) => taskSel.append(el('option', { value: t.id }, t.name)));
        taskSel.value = 'keep';
    };
    fillTask(null);
    const billable = el('select', {}, el('option', { value: 'keep' }, '— keep —'), el('option', { value: '1' }, 'Billable'), el('option', { value: '0' }, 'Not billable'));
    const date = el('input', { type: 'date' });
    const desc = el('input', { type: 'text', placeholder: '— keep descriptions —' });
    const add = categoryPicker([]);
    const remove = categoryPicker([]);
    openModal({
        title: `Edit ${ids.length} entries`,
        wide: true,
        body: el('div', { class: 'form-grid' },
            el('p', { class: 'full muted' }, 'Only the fields you change are updated. Invoiced entries are left alone.'),
            el('label', {}, 'Move to project', projSel), el('label', {}, 'Task', taskSel),
            el('label', {}, 'Add categories', add), el('label', {}, 'Remove categories', remove),
            el('label', {}, 'Billable', billable), el('label', {}, 'Move to date', date),
            el('label', { class: 'full' }, 'Replace description', desc)),
        actions: [{ label: 'Cancel' }, { label: 'Apply', class: 'primary', submit: true, run: async () => {
            const set = { add_category_ids: add.getValue(), remove_category_ids: remove.getValue(), billable: billable.value, task_id: taskSel.value };
            if (projSel.value) set.project_id = +projSel.value;
            if (date.value) set.date = date.value;
            if (desc.value.trim()) set.description = desc.value;
            const r = await api('entries.bulk_update', { ids, set });
            toast(`Updated ${r.updated}` + (r.skipped ? ` (${r.skipped} invoiced entries skipped)` : ''));
            onSaved();
        } }],
    });
}

function duplicateModal(ids, onSaved) {
    const date = el('input', { type: 'date', value: today() });
    openModal({
        title: `Copy ${ids.length} entries`,
        body: el('div', { class: 'form-grid' }, el('label', { class: 'full' }, 'Copy to date', date)),
        actions: [{ label: 'Cancel' }, { label: 'Copy', class: 'primary', submit: true, run: async () => {
            const r = await api('entries.duplicate', { ids, date: date.value });
            S.track.selected.clear();
            toast(`Copied ${r.ids.length} entries`);
            onSaved();
        } }],
    });
}

// ---------------------------------------------------------------------------
// Timesheet view: a week grid of hours per project/task

async function viewTimesheet() {
    const v = S.ts;
    if (!v.weekStart) v.weekStart = weekStart(today());
    const host = el('div');
    setView(host);
    await renderTimesheet(host);
}

async function renderTimesheet(host) {
    const v = S.ts;
    const dates = [...Array(7)].map((_, i) => addDays(v.weekStart, i));
    const { entries } = await api('entries.list', { from: dates[0], to: dates[6] });
    const rows = new Map();
    const keyOf = (p, t) => `${p}|${t || 0}`;
    const ensure = (p, t) => {
        const k = keyOf(p, t);
        if (!rows.has(k)) rows.set(k, { project_id: +p, task_id: t ? +t : null, days: {}, locked: {}, described: {} });
        return rows.get(k);
    };
    for (const e of entries) {
        const r = ensure(e.project_id, e.task_id);
        r.days[e.date] = (r.days[e.date] || 0) + e.minutes;
        if (e.invoice_id !== null) r.locked[e.date] = (r.locked[e.date] || 0) + e.minutes;
        if (e.description && e.invoice_id === null) r.described[e.date] = true;
    }
    for (const k of v.extraRows) { const [p, t] = k.split('|'); if (byId(S.projects, p)) ensure(p, +t || null); }
    const sorted = [...rows.values()].sort((a, b) => {
        const pa = byId(S.projects, a.project_id), pb = byId(S.projects, b.project_id);
        const ca = pa && clientOf(pa), cb = pb && clientOf(pb);
        return (ca?.name || '').localeCompare(cb?.name || '') || (pa?.name || '').localeCompare(pb?.name || '')
            || (byId(S.tasks, a.task_id)?.name || '').localeCompare(byId(S.tasks, b.task_id)?.name || '');
    });

    const rerender = async () => {
        const active = document.activeElement;
        const focusKey = active && active.dataset ? active.dataset.cell : null;
        const typed = focusKey && active.value !== active.dataset.orig ? active.value : null;
        await renderTimesheet(host);
        if (focusKey) {
            const i = host.querySelector(`[data-cell="${focusKey}"]`);
            if (i) { if (typed !== null) i.value = typed; i.focus(); i.select(); }
        }
    };

    const cell = (r, date) => {
        const m = r.days[date] || 0;
        const key = keyOf(r.project_id, r.task_id) + '|' + date;
        const input = el('input', {
            type: 'text', class: 'cell' + (r.locked[date] ? ' locked' : ''), value: m ? fmtDur(m) : '', placeholder: '–',
            title: r.locked[date] ? `${fmtDur(r.locked[date])} already invoiced` : 'Hours, e.g. 7.5 or 7:30',
            dataset: { cell: key, orig: m ? fmtDur(m) : '' },
        });
        input.addEventListener('focus', () => input.select());
        input.addEventListener('keydown', (ev) => { if (ev.key === 'Enter') { ev.preventDefault(); input.blur(); } });
        input.addEventListener('change', () => attempt(async () => {
            const target = input.value.trim() === '' ? 0 : parseDuration(input.value);
            if (target === null) { input.classList.add('invalid'); throw new Error('Type hours like 7.5 or 7:30'); }
            input.classList.remove('invalid');
            if (target < m && r.described[date]) {
                const ok = await confirmBox(`This reduces time on entries that have descriptions (${fmtDate(date)}). Continue?`, 'Reduce');
                if (!ok) { input.value = input.dataset.orig; return; }
            }
            await api('timesheet.set', { date, project_id: r.project_id, task_id: r.task_id, minutes: target });
            await rerender();
        }));
        return el('td', {}, input);
    };

    const dayTotals = dates.map((d) => sorted.reduce((a, r) => a + (r.days[d] || 0), 0));
    const weekTotal = dayTotals.reduce((a, b) => a + b, 0);
    const taskSel = el('select');
    let newProject = null;
    const newProjSel = projectSelect(null, { onchange: (id) => { newProject = id; fillTaskSelect(taskSel, id, null); } });
    fillTaskSelect(taskSel, null, null);
    const isThisWeek = v.weekStart === weekStart(today());
    const go = (ws) => { v.weekStart = ws; v.extraRows = []; renderTimesheet(host); };

    fill(host, 
        el('div', { class: 'toolbar' },
            el('div', { class: 'week-nav' },
                el('button', { class: 'btn ghost icon', onclick: () => go(addDays(v.weekStart, -7)) }, '‹'),
                el('button', { class: 'btn ghost', onclick: () => go(weekStart(today())) }, isThisWeek ? 'This week' : fmtRange(dates[0], dates[6])),
                el('button', { class: 'btn ghost icon', onclick: () => go(addDays(v.weekStart, 7)) }, '›'),
                el('input', { type: 'date', value: dates[0], onchange: (ev) => ev.target.value && go(weekStart(ev.target.value)) })),
            el('div', { class: 'spacer' }),
            el('button', { class: 'btn', onclick: () => attempt(async () => {
                const prev = await api('entries.list', { from: addDays(dates[0], -7), to: addDays(dates[0], -1) });
                const keys = [...new Set(prev.entries.map((e) => keyOf(e.project_id, e.task_id)))];
                if (!keys.length) { toast('Nothing was logged the week before.'); return; }
                v.extraRows = [...new Set([...v.extraRows, ...keys])];
                renderTimesheet(host);
            }) }, 'Copy rows from last week'),
            el('button', { class: 'btn', onclick: () => bulkAddModal(() => renderTimesheet(host), { from: dates[0], to: addDays(dates[0], 4) }) }, '⧉ Bulk add'),
            el('a', { class: 'btn', href: 'report.php?' + new URLSearchParams({ type: 'timesheet', from: dates[0], to: dates[6] }), target: '_blank' }, '🖨 Print week')),
        el('div', { class: 'card table-wrap' }, el('table', { class: 'grid' },
            el('thead', {}, el('tr', {}, el('th', {}, 'Project'), el('th', {}, 'Task'),
                dates.map((d) => el('th', { class: 'num' + (d === today() ? ' today' : '') }, dayName(d), el('br'), el('small', {}, fmtDate(d).slice(0, 5)))),
                el('th', { class: 'num' }, 'Total'))),
            el('tbody', {}, sorted.length ? sorted.map((r) => el('tr', {},
                el('td', {}, projectChip(r.project_id)),
                el('td', { class: 'muted' }, r.task_id ? byId(S.tasks, r.task_id)?.name : '—'),
                dates.map((d) => cell(r, d)),
                el('td', { class: 'num total' }, fmtDur(Object.values(r.days).reduce((a, b) => a + b, 0)))))
                : el('tr', {}, el('td', { colspan: 10, class: 'muted empty' }, 'No time this week. Add a row below, or copy last week’s rows.'))),
            el('tfoot', {}, el('tr', {}, el('th', { colspan: 2 }, 'Total'), dayTotals.map((m) => el('th', { class: 'num' }, m ? fmtDur(m) : '')),
                el('th', { class: 'num' }, fmtDur(weekTotal)))))),
        el('div', { class: 'card add-row' },
            el('span', {}, 'Add a row: '), newProjSel, taskSel,
            el('button', { class: 'btn primary', onclick: () => {
                if (!newProject) { toast('Choose a project', true); return; }
                v.extraRows.push(keyOf(newProject, taskSel.value || 0));
                renderTimesheet(host);
            } }, 'Add row')),
        el('p', { class: 'muted small' }, 'Type the total hours for each day (7.5, 7:30 or 450m). Lowering or clearing a cell trims that day’s entries; invoiced time is locked. Use the Track page to add descriptions and categories.'),
    );
}

// ---------------------------------------------------------------------------
// Reports view

function reportQuery() {
    const r = S.rep;
    const q = { from: r.from, to: r.to };
    for (const k of ['client_id', 'project_id', 'category_id', 'billable']) if (r[k] !== '') q[k] = r[k];
    return q;
}

async function viewReports() {
    const r = S.rep;
    if (r.preset !== 'custom') [r.from, r.to] = periodRange(r.preset);
    const host = el('div');
    setView(host);
    await renderReports(host);
}

function periodControls(state, onchange) {
    const preset = el('select', { onchange: () => {
        state.preset = preset.value;
        if (state.preset !== 'custom') [state.from, state.to] = periodRange(state.preset);
        onchange();
    } }, PERIODS.map(([v, l]) => el('option', { value: v }, l)));
    preset.value = state.preset;
    const from = el('input', { type: 'date', value: state.from, onchange: () => { state.from = from.value; state.preset = 'custom'; onchange(); } });
    const to = el('input', { type: 'date', value: state.to, onchange: () => { state.to = to.value; state.preset = 'custom'; onchange(); } });
    const shift = (dir) => { [state.from, state.to] = shiftPeriod(state.from, state.to, dir); state.preset = 'custom'; onchange(); };
    return [
        el('button', { class: 'btn ghost icon', title: 'Previous period', onclick: () => shift(-1), disabled: !state.from }, '‹'),
        preset,
        el('button', { class: 'btn ghost icon', title: 'Next period', onclick: () => shift(1), disabled: !state.from }, '›'),
        from, el('span', { class: 'muted' }, 'to'), to,
    ];
}

async function renderReports(host) {
    const r = S.rep;
    const rerender = () => renderReports(host);
    const { summary: s } = await api('report.summary', reportQuery());
    const projSel = el('select', { onchange: () => { r.project_id = projSel.value; rerender(); } }, el('option', { value: '' }, 'All projects'),
        S.projects.filter((p) => !r.client_id || p.client_id === +r.client_id).map((p) => el('option', { value: p.id }, p.name + (r.client_id ? '' : ` · ${clientOf(p)?.name}`))));
    projSel.value = r.project_id;
    const billSel = el('select', { onchange: () => { r.billable = billSel.value; rerender(); } },
        el('option', { value: '' }, 'Billable & non-billable'), el('option', { value: '1' }, 'Billable only'), el('option', { value: '0' }, 'Non-billable only'));
    billSel.value = r.billable;
    const qs = new URLSearchParams(reportQuery()).toString();

    const bars = (title, rows, extra) => {
        if (!rows.length) return null;
        const max = Math.max(...rows.map((x) => x.minutes), 1);
        return el('section', { class: 'card' }, el('h3', {}, title), el('table', { class: 'breakdown' },
            el('thead', {}, el('tr', {}, el('th', {}, ''), el('th', {}, ''), el('th', { class: 'num' }, 'Hours'), el('th', { class: 'num' }, 'Billable'), el('th', { class: 'num' }, 'Amount'))),
            el('tbody', {}, rows.map((x) => el('tr', {},
                el('td', {}, x.color ? el('span', { class: 'dot', style: { background: x.color } }) : null, x.label, extra && x[extra] ? el('span', { class: 'muted' }, ' · ' + x[extra]) : null),
                el('td', { class: 'bar-cell' }, el('span', { class: 'bar', style: { width: (x.minutes / max * 100) + '%', background: x.color || null } })),
                el('td', { class: 'num' }, fmtHrs(x.minutes)),
                el('td', { class: 'num muted' }, fmtHrs(x.billable_minutes)),
                el('td', { class: 'num' }, money(x.amount)))))));
    };
    const days = s.days.map((d) => ({ ...d, label: dayName(d.key) + ' ' + fmtDate(d.key) }));

    fill(host, 
        el('div', { class: 'toolbar filters' }, periodControls(r, rerender),
            clientSelect(r.client_id, 'All clients', (v) => { r.client_id = v; r.project_id = ''; rerender(); }),
            projSel,
            categorySelect(r.category_id, 'All categories', (v) => { r.category_id = v; rerender(); }),
            billSel),
        el('div', { class: 'stats' },
            el('div', { class: 'card stat' }, el('span', {}, 'Total time'), el('strong', {}, fmtDur(s.minutes)), el('small', { class: 'muted' }, fmtHrs(s.minutes) + ' h')),
            el('div', { class: 'card stat' }, el('span', {}, 'Billable'), el('strong', {}, fmtDur(s.billable_minutes)), el('small', { class: 'muted' }, s.minutes ? Math.round(s.billable_minutes / s.minutes * 100) + '%' : '')),
            el('div', { class: 'card stat' }, el('span', {}, 'Billable amount'), el('strong', {}, money(s.amount))),
            el('div', { class: 'card stat' }, el('span', {}, 'Not yet invoiced'), el('strong', {}, money(s.uninvoiced_amount)))),
        el('div', { class: 'toolbar' },
            el('a', { class: 'btn', target: '_blank', href: 'report.php?type=timesheet&' + qs }, '🖨 Timesheet'),
            el('a', { class: 'btn', target: '_blank', href: 'report.php?type=summary&amounts=1&' + qs }, '🖨 Detailed report'),
            el('a', { class: 'btn', href: 'report.php?format=csv&' + qs }, '⬇ CSV'),
            el('div', { class: 'spacer' }),
            s.uninvoiced_amount > 0 ? el('button', { class: 'btn primary', onclick: () => {
                S.inv = { ...newInvoiceState(), client_id: r.client_id, preset: r.preset, from: r.from, to: r.to };
                location.hash = '#invoices';
            } }, 'Invoice this time…') : null),
        s.count ? el('div', { class: 'report-grid' },
            S.rep.client_id ? null : bars('By client', s.clients),
            bars('By project', s.projects, S.rep.client_id ? null : 'client'),
            bars('By category', s.categories),
            bars('By task', s.tasks, 'project'),
            bars('By day', days)) : el('p', { class: 'empty muted' }, 'No time recorded for these filters.'),
    );
}

// ---------------------------------------------------------------------------
// Invoices view

function newInvoiceState() {
    const [from, to] = periodRange('last_month');
    return { client_id: '', preset: 'last_month', from, to, group: 'project', issue_date: today(), due_date: '', number: '', tax_rate: '', bill_to: '', notes: '', extra: [] };
}

async function viewInvoices() {
    if (!S.inv) S.inv = newInvoiceState();
    const host = el('div');
    setView(host);
    await renderInvoices(host);
}

function termsFor(clientId) {
    const c = byId(S.clients, clientId);
    return c && c.payment_terms !== null && c.payment_terms !== '' ? +c.payment_terms : +S.settings.payment_terms || 0;
}

async function renderInvoices(host) {
    const v = S.inv;
    if (!v.due_date) v.due_date = addDays(v.issue_date, termsFor(v.client_id));
    const { invoices } = await api('invoices.list');
    const previewBox = el('div', { class: 'invoice-preview' });

    const refreshPreview = debounce(() => attempt(async () => {
        const p = await api('invoices.preview', invoicePayload());
        renderPreview(previewBox, p);
    }), 250);
    const invoicePayload = () => ({
        client_id: v.client_id || null, from: v.from || null, to: v.to || null, group: v.group, issue_date: v.issue_date, due_date: v.due_date,
        number: v.number, tax_rate: v.tax_rate, bill_to: v.bill_to, notes: v.notes, extra_lines: v.extra,
    });
    const bind = (input, key, after) => {
        input.value = v[key] ?? '';
        input.addEventListener('input', () => { v[key] = input.value; after && after(); refreshPreview(); });
        return input;
    };
    const clientSel = clientSelect(v.client_id, 'All clients (e.g. umbrella company)', (val) => {
        v.client_id = val;
        v.due_date = addDays(v.issue_date, termsFor(val));
        renderInvoices(host);
    });
    const group = bind(el('select', {}, el('option', { value: 'project' }, 'One line per project'), el('option', { value: 'task' }, 'One line per project & task'),
        el('option', { value: 'day' }, 'One line per day'), el('option', { value: 'entry' }, 'One line per time entry')), 'group');
    const issue = bind(el('input', { type: 'date' }), 'issue_date', () => { v.due_date = addDays(v.issue_date, termsFor(v.client_id)); due.value = v.due_date; });
    const due = bind(el('input', { type: 'date' }), 'due_date');
    const number = bind(el('input', { type: 'text', placeholder: 'Automatic' }), 'number');
    const client = byId(S.clients, v.client_id);
    const defaultTax = client && client.tax_rate !== null && client.tax_rate !== '' ? client.tax_rate : S.settings.tax_rate;
    const tax = bind(el('input', { type: 'number', step: '0.01', placeholder: `${defaultTax}% (default)` }), 'tax_rate');
    const autoBillTo = client ? [client.name, client.contact ? 'Attn: ' + client.contact : '', client.address].filter(Boolean).join('\n') : S.settings.umbrella_bill_to;
    const billTo = bind(el('textarea', { rows: 4, placeholder: autoBillTo || 'Who the invoice is addressed to' }), 'bill_to');
    const notes = bind(el('textarea', { rows: 2, placeholder: 'e.g. PO number, thank-you note' }), 'notes');

    const extraBox = el('div', { class: 'extra-lines' });
    const renderExtra = () => {
        fill(extraBox, ...v.extra.map((l, i) => el('div', { class: 'extra-line' },
            el('input', { type: 'text', placeholder: 'Description (e.g. travel expenses)', value: l.description, oninput: (ev) => { l.description = ev.target.value; refreshPreview(); } }),
            el('input', { type: 'number', step: '0.01', class: 'qty', placeholder: 'Qty', value: l.quantity, oninput: (ev) => { l.quantity = ev.target.value; refreshPreview(); } }),
            el('input', { type: 'number', step: '0.01', class: 'price', placeholder: 'Price', value: l.unit_price, oninput: (ev) => { l.unit_price = ev.target.value; refreshPreview(); } }),
            el('button', { class: 'btn ghost icon', title: 'Remove', onclick: () => { v.extra.splice(i, 1); renderExtra(); refreshPreview(); } }, '✕'))),
        el('button', { class: 'btn small ghost', onclick: () => { v.extra.push({ description: '', quantity: 1, unit_price: '' }); renderExtra(); } }, '＋ Add an expense or other line'));
    };
    renderExtra();

    const today_ = today();
    const statusSel = (inv) => {
        const s = el('select', { class: 'status ' + inv.status, onchange: () => attempt(async () => {
            await api('invoices.update', { id: inv.id, status: s.value });
            toast('Marked as ' + s.value);
            renderInvoices(host);
        }) }, ['draft', 'sent', 'paid'].map((x) => el('option', { value: x }, x[0].toUpperCase() + x.slice(1))));
        s.value = inv.status;
        return s;
    };
    const outstanding = invoices.filter((i) => i.status !== 'paid').reduce((a, i) => a + +i.total, 0);

    fill(host, 
        el('section', { class: 'card' },
            el('h2', {}, 'New invoice'),
            el('div', { class: 'form-grid' },
                el('label', {}, 'Bill', clientSel),
                el('label', {}, 'Lines', group),
                el('div', { class: 'full period-row' }, el('span', {}, 'Time from '), periodControls(v, () => renderInvoices(host))),
                el('label', {}, 'Invoice date', issue), el('label', {}, 'Due date', due),
                el('label', {}, 'Invoice number', number), el('label', {}, `${S.settings.tax_label || 'Tax'} rate %`, tax),
                el('label', {}, 'Bill to (leave blank to use the client’s address)', billTo), el('label', {}, 'Notes', notes),
                el('div', { class: 'full' }, extraBox)),
            previewBox,
            el('div', { class: 'modal-actions' },
                el('button', { class: 'btn', onclick: () => { S.inv = newInvoiceState(); renderInvoices(host); } }, 'Reset'),
                el('button', { class: 'btn primary', onclick: () => attempt(async () => {
                    const r = await api('invoices.create', invoicePayload());
                    await loadBootstrap();
                    toast('Invoice ' + r.number + ' created');
                    S.inv = newInvoiceState();
                    window.open('invoice.php?id=' + r.id, '_blank');
                    renderInvoices(host);
                }) }, 'Create invoice'))),
        el('section', { class: 'card' },
            el('div', { class: 'toolbar' }, el('h2', {}, 'Invoices'), el('div', { class: 'spacer' }),
                el('span', {}, 'Outstanding: ', el('strong', {}, money(outstanding)))),
            invoices.length ? el('div', { class: 'table-wrap' }, el('table', { class: 'list' },
                el('thead', {}, el('tr', {}, ['Number', 'Date', 'Client', 'Period', 'Due', 'Total', 'Status', ''].map((h) => el('th', { class: h === 'Total' ? 'num' : '' }, h)))),
                el('tbody', {}, invoices.map((inv) => el('tr', { class: inv.status !== 'paid' && inv.due_date && inv.due_date < today_ ? 'overdue' : '' },
                    el('td', {}, el('a', { href: 'invoice.php?id=' + inv.id, target: '_blank' }, inv.number)),
                    el('td', {}, fmtDate(inv.issue_date)),
                    el('td', {}, inv.client_name || el('em', {}, 'All clients')),
                    el('td', { class: 'muted' }, inv.period_from || inv.period_to ? `${fmtDate(inv.period_from)} – ${fmtDate(inv.period_to)}` : ''),
                    el('td', {}, fmtDate(inv.due_date), inv.status !== 'paid' && inv.due_date && inv.due_date < today_ ? el('span', { class: 'badge danger' }, 'overdue') : null),
                    el('td', { class: 'num' }, money(+inv.total)),
                    el('td', {}, statusSel(inv)),
                    el('td', { class: 'row-actions' },
                        el('a', { class: 'btn small ghost', href: 'invoice.php?id=' + inv.id, target: '_blank' }, 'View'),
                        el('a', { class: 'btn small ghost', href: 'invoice.php?timesheet=1&id=' + inv.id, target: '_blank' }, '+ timesheet'),
                        el('button', { class: 'btn small ghost', onclick: () => editInvoiceModal(inv, () => renderInvoices(host)) }, 'Edit'),
                        el('button', { class: 'btn small ghost danger', onclick: async () => {
                            if (!await confirmBox(`Delete invoice ${inv.number}? Its ${inv.entry_count} time entries become un-invoiced again so you can re-bill them.`)) return;
                            attempt(async () => { await api('invoices.delete', { id: inv.id }); toast('Invoice deleted'); renderInvoices(host); });
                        } }, 'Delete'))))))) : el('p', { class: 'muted empty' }, 'No invoices yet.')),
    );
    refreshPreview();
}

function renderPreview(box, p) {
    if (!p.lines.length && !p.subtotal) {
        fill(box, el('p', { class: 'muted empty' }, 'No un-invoiced billable time for this client and period.'));
        return;
    }
    fill(box, ...[
        el('h3', {}, `Preview — ${p.entry_count} time entries`),
        el('table', { class: 'list' },
            el('thead', {}, el('tr', {}, el('th', {}, 'Description'), el('th', { class: 'num' }, 'Qty'), el('th', { class: 'num' }, 'Rate'), el('th', { class: 'num' }, 'Amount'))),
            el('tbody', {}, p.lines.map((l) => el('tr', {}, el('td', {}, l.description), el('td', { class: 'num' }, fmtQty(l.quantity, l.unit)),
                el('td', { class: 'num' }, money(l.unit_price)), el('td', { class: 'num' }, money(l.amount))))),
            el('tfoot', {},
                el('tr', {}, el('th', { colspan: 3 }, 'Subtotal'), el('td', { class: 'num' }, money(p.subtotal))),
                p.tax_rate ? el('tr', {}, el('th', { colspan: 3 }, `${S.settings.tax_label || 'Tax'} @ ${p.tax_rate}%`), el('td', { class: 'num' }, money(p.tax))) : null,
                el('tr', { class: 'grand' }, el('th', { colspan: 3 }, 'Total'), el('td', { class: 'num' }, money(p.total))))),
        p.lines.some((l) => !l.unit_price) ? el('p', { class: 'warn' }, 'Some lines have no rate. Set an hourly rate on the client, project or task (or a default in Settings).') : null,
    ].filter(Boolean));
}

function editInvoiceModal(inv, onSaved) {
    const f = formFields([
        { name: 'number', label: 'Invoice number' },
        { name: 'issue_date', label: 'Invoice date', type: 'date' },
        { name: 'due_date', label: 'Due date', type: 'date' },
        { name: 'bill_to', label: 'Bill to', type: 'textarea', rows: 4 },
        { name: 'notes', label: 'Notes', type: 'textarea' },
    ], inv);
    openModal({
        title: 'Edit invoice ' + inv.number, body: f,
        actions: [{ label: 'Cancel' }, { label: 'Save', class: 'primary', submit: true, run: async () => {
            await api('invoices.update', { id: inv.id, ...f.values() }); toast('Saved'); onSaved();
        } }],
    });
}

// ---------------------------------------------------------------------------
// Clients, projects, tasks & categories

const BILLING_UNITS = [['hour', 'Hourly'], ['half_day', 'Half days (any time = at least ½ day)'], ['day', 'Full days (any time = 1 day)']];

function clientRateText(c) {
    if (c.billing_unit === 'hour') {
        return c.rate !== null && c.rate !== '' ? money(+c.rate) + '/h' : el('span', { class: 'muted' }, `default ${money(+S.settings.default_rate)}/h`);
    }
    const rate = c.rate !== null && c.rate !== '' ? +c.rate : +S.settings.default_day_rate;
    return [money(rate) + '/day', el('div', { class: 'muted small' }, c.billing_unit === 'half_day' ? 'billed in half days' : 'billed in whole days')];
}

const RECORDS = {
    clients: {
        title: 'Client',
        fields: () => [
            { name: 'name', label: 'Name', required: true },
            { name: 'contact', label: 'Contact person' },
            { name: 'email', label: 'Email', type: 'email' },
            { name: 'billing_unit', label: 'Bill by', type: 'select', options: BILLING_UNITS },
            { name: 'rate', label: 'Rate (per hour, or per day for day billing)', type: 'number', step: '0.01',
                placeholder: `Default ${S.settings.default_rate}/hour or ${S.settings.default_day_rate}/day` },
            { name: 'day_hours', label: 'Working day (hours)', type: 'number', step: '0.25', placeholder: `Default (${S.settings.day_hours})`,
                hint: 'Half-day billing: up to half of this is ½ day; more is a full day.' },
            { name: 'address', label: 'Invoice address', type: 'textarea' },
            { name: 'tax_rate', label: `${S.settings.tax_label || 'Tax'} rate %`, type: 'number', step: '0.01', placeholder: `Default (${S.settings.tax_rate}%)` },
            { name: 'payment_terms', label: 'Payment terms (days)', type: 'number', placeholder: `Default (${S.settings.payment_terms})` },
            { name: 'invoice_notes', label: 'Always add to this client’s invoices', type: 'textarea', rows: 2 },
            { name: 'archived', label: 'Archived (hidden from pickers)', type: 'checkbox' },
        ],
    },
    projects: {
        title: 'Project',
        fields: () => [
            { name: 'client_id', label: 'Client', type: 'select', options: [['', 'Choose…'], ...S.clients.filter((c) => !c.archived).map((c) => [c.id, c.name])] },
            { name: 'name', label: 'Project name', required: true },
            { name: 'code', label: 'Code / PO (optional)' },
            { name: 'rate', label: 'Hourly rate', type: 'number', step: '0.01', placeholder: 'Use the client’s rate' },
            { name: 'color', label: 'Colour', type: 'color', default: randomColor() },
            { name: 'billable', label: 'Time is billable by default', type: 'checkbox', default: 1 },
            { name: 'archived', label: 'Archived', type: 'checkbox' },
        ],
    },
    tasks: {
        title: 'Task',
        fields: () => [
            { name: 'project_id', label: 'Project', type: 'select', options: [['', 'Any project (general task)'], ...S.projects.filter((p) => !p.archived).map((p) => [p.id, `${clientOf(p)?.name} · ${p.name}`])] },
            { name: 'name', label: 'Task name', required: true },
            { name: 'rate', label: 'Hourly rate', type: 'number', step: '0.01', placeholder: 'Use the project’s rate' },
            { name: 'archived', label: 'Archived', type: 'checkbox' },
        ],
    },
    categories: {
        title: 'Category',
        fields: () => [
            { name: 'name', label: 'Name', required: true },
            { name: 'color', label: 'Colour', type: 'color', default: randomColor() },
            { name: 'archived', label: 'Archived', type: 'checkbox' },
        ],
    },
};

function editRecord(type, record, onSaved, defaults = {}) {
    const def = RECORDS[type];
    if (type === 'projects' && !S.clients.length) {
        toast('Add a client first', true);
        return editRecord('clients', null, () => editRecord('projects', null, onSaved));
    }
    const f = formFields(def.fields(), record || defaults);
    const actions = [{ label: 'Cancel' }, { label: 'Save', class: 'primary', submit: true, run: async () => {
        const r = await api(type + '.save', { ...f.values(), id: record ? record.id : null });
        await loadBootstrap();
        toast(def.title + ' saved');
        onSaved && onSaved(r.id);
    } }];
    if (record) {
        actions.unshift({ label: 'Delete', class: 'danger ghost', run: async () => {
            if (!await confirmBox(`Delete ${def.title.toLowerCase()} “${record.name}”?`)) return false;
            await api(type + '.delete', { id: record.id });
            await loadBootstrap();
            toast('Deleted');
            onSaved && onSaved(null);
        } });
    }
    openModal({ title: (record ? 'Edit ' : 'New ') + def.title.toLowerCase(), body: f, actions, wide: type === 'clients' });
    setTimeout(() => f.querySelector('input[type=text]')?.focus(), 50);
}

function viewManage() {
    const re = () => viewManage();
    const vis = (r) => S.showArchived || !r.archived;
    const rateText = (v, fallback) => (v !== null && v !== '' ? money(+v) + '/h' : el('span', { class: 'muted' }, fallback));
    const table = (heads, rows) => rows.length
        ? el('div', { class: 'table-wrap' }, el('table', { class: 'list clickable' }, el('thead', {}, el('tr', {}, heads.map((h) => el('th', {}, h)))), el('tbody', {}, rows)))
        : el('p', { class: 'muted empty' }, 'None yet.');
    const archivedBadge = (r) => r.archived ? el('span', { class: 'badge' }, 'archived') : null;
    const section = (type, title, heads, rows, extraBtn) => el('section', { class: 'card' },
        el('div', { class: 'toolbar' }, el('h2', {}, title), el('div', { class: 'spacer' }), extraBtn,
            el('button', { class: 'btn primary small', onclick: () => editRecord(type, null, re) }, '＋ New ' + RECORDS[type].title.toLowerCase())),
        table(heads, rows));

    setView(el('div', { class: 'manage' },
        el('div', { class: 'toolbar' }, el('label', { class: 'check' },
            el('input', { type: 'checkbox', checked: S.showArchived, onchange: (ev) => { S.showArchived = ev.target.checked; re(); } }), 'Show archived')),
        section('clients', 'Clients', ['Name', 'Contact', 'Rate', 'Projects', ''],
            S.clients.filter(vis).map((c) => el('tr', { onclick: () => editRecord('clients', c, re) },
                el('td', {}, el('strong', {}, c.name), archivedBadge(c)),
                el('td', {}, c.contact || '', c.email ? el('div', { class: 'muted' }, c.email) : null),
                el('td', {}, clientRateText(c)),
                el('td', {}, S.projects.filter((p) => p.client_id === c.id && !p.archived).length),
                el('td', { class: 'row-actions' }, el('button', { class: 'btn small ghost', onclick: (ev) => { ev.stopPropagation(); editRecord('projects', null, re, { client_id: c.id }); } }, '＋ project'))))),
        section('projects', 'Projects', ['Project', 'Client', 'Rate', 'Billable'],
            S.projects.filter((p) => vis(p) && (S.showArchived || !clientOf(p)?.archived)).map((p) => el('tr', { onclick: () => editRecord('projects', p, re) },
                el('td', {}, projectChip(p.id), p.code ? el('span', { class: 'muted' }, ` (${p.code})`) : null, archivedBadge(p)),
                el('td', {}, clientOf(p)?.name),
                el('td', {}, clientOf(p)?.billing_unit !== 'hour' ? el('span', { class: 'muted' }, 'client day rate') : rateText(p.rate, 'client rate')),
                el('td', {}, p.billable ? 'Yes' : 'No')))),
        section('tasks', 'Tasks', ['Task', 'Project', 'Rate'],
            S.tasks.filter(vis).map((t) => el('tr', { onclick: () => editRecord('tasks', t, re) },
                el('td', {}, t.name, archivedBadge(t)),
                el('td', {}, t.project_id ? projectChip(t.project_id) : el('span', { class: 'muted' }, 'Any project')),
                el('td', {}, rateText(t.rate, 'project rate'))))),
        section('categories', 'Categories', ['Category'],
            S.categories.filter(vis).map((c) => el('tr', { onclick: () => editRecord('categories', c, re) },
                el('td', {}, catChip(c), archivedBadge(c))))),
        el('p', { class: 'muted small' }, 'Hourly clients: an entry uses its own rate override, else its task’s rate, else the project’s, else the client’s, else the default in Settings. Half-day and day clients are always charged the client’s day rate, counted per date across all their projects. Items with history can’t be deleted — archive them instead.'),
    ));
}

// ---------------------------------------------------------------------------
// Settings

// Passkeys: list, add (this device / a phone or security key) and remove.
function securitySection() {
    const box = el('section', { class: 'card', id: 'security' });
    const render = async () => {
        const { passkeys, supported_here: domainOk } = await api('passkeys.list');
        const canAdd = Passkey.supported() && domainOk;
        fill(box, 
            el('h2', {}, 'Sign-in & security'),
            !passkeys.length ? el('p', { class: 'warn' }, 'You have no passkeys yet. Add one so you can sign in with your fingerprint, face or device PIN.') : null,
            el('p', { class: 'muted' }, 'Sign in with a passkey. If you can’t, the backup is your password plus a code sent to your email address.'),
            passkeys.length ? el('table', { class: 'list' },
                el('thead', {}, el('tr', {}, el('th', {}, 'Passkey'), el('th', {}, 'Added'), el('th', {}, 'Last used'), el('th', {}, ''))),
                el('tbody', {}, passkeys.map((k) => el('tr', {},
                    el('td', {}, '🔑 ' + k.name),
                    el('td', {}, fmtDate(k.created_at.slice(0, 10))),
                    el('td', { class: 'muted' }, k.last_used_at ? fmtDate(k.last_used_at.slice(0, 10)) : 'never'),
                    el('td', { class: 'row-actions' }, el('button', { class: 'btn small ghost danger', onclick: async () => {
                        if (!await confirmBox(`Remove the passkey “${k.name}”? You won’t be able to sign in with it any more.`, 'Remove')) return;
                        attempt(async () => { await api('passkeys.delete', { id: k.id }); toast('Passkey removed'); render(); });
                    } }, 'Remove')))))) : null,
            el('div', { class: 'modal-actions' },
                canAdd ? null : el('span', { class: 'muted small' }, Passkey.supported()
                    ? 'Passkeys need a domain name — open this app via its web address or http://localhost, not an IP address.'
                    : 'This browser can’t create passkeys here (they need HTTPS or localhost).'),
                el('button', { class: 'btn primary', disabled: !canAdd, onclick: () => addPasskey(render) }, '＋ Add a passkey')),
        );
    };
    render().catch((e) => fill(box, el('p', { class: 'error' }, e.message)));
    return box;
}

function addPasskey(onDone) {
    const name = el('input', { type: 'text', value: Passkey.deviceName() });
    openModal({
        title: 'Add a passkey',
        body: el('div', { class: 'form-grid' },
            el('label', { class: 'full' }, 'Name (so you can tell your passkeys apart)', name),
            el('p', { class: 'full muted' }, 'Your browser will ask you to confirm with your fingerprint, face, device PIN or security key. To use your phone, pick “use a phone or tablet” when asked.')),
        actions: [{ label: 'Cancel' }, { label: 'Continue', class: 'primary', submit: true, run: async () => {
            const options = await api('passkeys.options');
            const cred = await Passkey.create(options);
            await api('passkeys.register', { ...cred, name: name.value });
            toast('Passkey added');
            onDone();
        } }],
    });
}

// A settings card with its own Save button; saves only its own fields.
function settingsCard(title, intro, defs) {
    const f = formFields(defs, S.settings);
    return el('section', { class: 'card' }, el('h2', {}, title), intro ? el('p', { class: 'muted' }, intro) : null, f,
        el('div', { class: 'modal-actions' }, el('button', { class: 'btn primary', onclick: () => attempt(async () => {
            const r = await api('settings.save', f.values());
            S.settings = r.settings;
            showWho();
            toast(title + ' saved');
        }) }, 'Save')));
}

// Email delivery status (no addresses or keys are ever sent to the browser) and a test button.
function emailSection() {
    const box = el('section', { class: 'card' });
    api('mail.status').then((st) => fill(box,
        el('h2', {}, 'Email delivery'),
        el('p', { class: 'muted' }, 'Sign-in codes and security alerts are emailed to your private sign-in address. '
            + 'The address and mail service details (e.g. your Brevo key) are kept in config.php on the server, never in the app or its backups.'),
        el('p', {}, 'Sending with: ', el('strong', {}, st.method), ' ',
            st.configured ? el('span', { class: 'badge ok' }, 'configured') : el('span', { class: 'badge danger' }, 'incomplete — see config.php')),
        el('div', { class: 'modal-actions' }, el('button', { class: 'btn', onclick: (ev) => attempt(async () => {
            ev.target.disabled = true;
            try {
                await api('mail.test');
                toast('Test email sent to your sign-in address');
            } finally { ev.target.disabled = false; }
        }) }, 'Send a test email')),
    )).catch((e) => fill(box, el('p', { class: 'error' }, e.message)));
    return box;
}

function viewSettings() {
    let zones = [];
    try { zones = Intl.supportedValuesOf('timeZone'); } catch (e) { /* older browser */ }
    const cur = el('input', { type: 'password', autocomplete: 'current-password' });
    const nw = el('input', { type: 'password', autocomplete: 'new-password' });
    setView(el('div', { class: 'settings' },
        settingsCard('Your details', 'Shown on your invoices and timesheets.', [
            { name: 'your_name', label: 'Full name' },
            { name: 'business_name', label: 'Company name' },
            { name: 'email', label: 'Contact email', type: 'email', hint: 'Printed on invoices for clients. Not used for signing in.' },
            { name: 'phone', label: 'Phone' },
            { name: 'address', label: 'Address', type: 'textarea' },
            { name: 'website', label: 'Website' },
            { name: 'tax_number', label: 'VAT / tax registration number' },
            { name: 'company_number', label: 'Company number' },
        ]),
        settingsCard('Rates & invoicing', null, [
            { name: 'currency_code', label: 'Currency code', placeholder: 'GBP, EUR, USD…' },
            { name: 'currency_symbol', label: 'Currency symbol' },
            { name: 'default_rate', label: 'Default hourly rate', type: 'number', step: '0.01', hint: 'Used when a client, project or task has no rate.' },
            { name: 'default_day_rate', label: 'Default day rate', type: 'number', step: '0.01', hint: 'For clients billed by the day or half day.' },
            { name: 'day_hours', label: 'Working day (hours)', type: 'number', step: '0.25', hint: 'Can be changed per client.' },
            { name: 'payment_terms', label: 'Payment terms (days)', type: 'number' },
            { name: 'tax_label', label: 'Tax name', placeholder: 'VAT' },
            { name: 'tax_rate', label: 'Default tax rate %', type: 'number', step: '0.01', hint: 'Use 0 if you are not registered.' },
            { name: 'invoice_prefix', label: 'Invoice number prefix', hint: 'You can use {YYYY}, {YY} and {MM}.' },
            { name: 'invoice_next', label: 'Next invoice number', type: 'number', min: 1 },
            { name: 'invoice_digits', label: 'Pad numbers to digits', type: 'number', min: 1 },
            { name: 'bank_details', label: 'Payment / bank details', type: 'textarea', placeholder: 'Account name, sort code, account number…' },
            { name: 'invoice_footer', label: 'Invoice footer', type: 'textarea', rows: 2 },
            { name: 'umbrella_bill_to', label: 'Bill-to for “all clients” invoices (e.g. your umbrella company)', type: 'textarea' },
        ]),
        settingsCard('Preferences', null, [
            { name: 'date_format', label: 'Date format', type: 'select', options: [['d/m/Y', '31/12/2026'], ['m/d/Y', '12/31/2026'], ['Y-m-d', '2026-12-31'], ['d.m.Y', '31.12.2026']] },
            { name: 'week_start', label: 'Week starts on', type: 'select', options: [['1', 'Monday'], ['7', 'Sunday'], ['6', 'Saturday']] },
            { name: 'timezone', label: 'Time zone', list: 'tz-list', hint: 'Used for the timer.' },
        ]),
        el('datalist', { id: 'tz-list' }, zones.map((z) => el('option', { value: z }))),
        securitySection(),
        el('section', { class: 'card' }, el('h2', {}, 'Password'),
            el('p', { class: 'muted' }, 'Your password is only used for the backup sign-in, together with a code sent to your email.'),
            el('div', { class: 'form-grid' }, el('label', {}, 'Current password', cur), el('label', {}, 'New password (10+ characters)', nw)),
            el('div', { class: 'modal-actions' }, el('button', { class: 'btn', onclick: () => attempt(async () => {
                await api('password.change', { current: cur.value, new: nw.value });
                cur.value = ''; nw.value = '';
                toast('Password changed');
            }) }, 'Change password'))),
        emailSection(),
        el('section', { class: 'card' }, el('h2', {}, 'Backup & export'),
            el('p', {}, 'Everything is stored in one small SQLite file. Download a copy now and then.'),
            el('div', { class: 'toolbar' },
                el('a', { class: 'btn', href: 'backup.php?t=' + encodeURIComponent(S.csrf) }, '⬇ Download database backup'),
                el('a', { class: 'btn', href: 'report.php?format=csv' }, '⬇ Export all time as CSV'))),
    ));
}

// Your name (or company) next to "Sign out".
function showWho() {
    const who = $('#who');
    if (who) who.textContent = S.settings.your_name || S.settings.business_name || '';
}

// ---------------------------------------------------------------------------
// Router

const VIEWS = { track: viewTrack, timesheet: viewTimesheet, reports: viewReports, invoices: viewInvoices, manage: viewManage, settings: viewSettings };

async function route() {
    const name = (location.hash.slice(1) || 'track').split('?')[0];
    const view = VIEWS[name] || viewTrack;
    $$('#nav a').forEach((a) => a.classList.toggle('active', a.getAttribute('href') === '#' + name));
    try {
        await view();
    } catch (e) {
        setView(el('div', { class: 'card error' }, 'Something went wrong: ' + e.message));
    }
}

window.addEventListener('hashchange', route);
$('#running-indicator').addEventListener('click', () => { location.hash = '#track'; });

// Pick up a timer started or stopped on another computer when coming back to this tab.
document.addEventListener('visibilitychange', () => {
    if (document.visibilityState !== 'visible' || $('dialog[open]')) return;
    attempt(async () => {
        const before = JSON.stringify(S.timer);
        await loadBootstrap();
        if (JSON.stringify(S.timer) !== before && (location.hash || '#track') === '#track') route();
        tick();
    });
});

(async () => {
    try {
        await loadBootstrap();
    } catch (e) {
        setView(el('div', { class: 'card error' }, 'Could not load: ' + e.message));
        return;
    }
    tick();
    showWho();
    route();
})();
