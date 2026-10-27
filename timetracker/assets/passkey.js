'use strict';
// Browser side of passkeys: converts between the server's JSON (base64url strings)
// and the ArrayBuffers that navigator.credentials expects.

const Passkey = (() => {
    const toBuf = (s) => {
        const b = atob(s.replace(/-/g, '+').replace(/_/g, '/') + '==='.slice((s.length + 3) % 4));
        return Uint8Array.from(b, (c) => c.charCodeAt(0)).buffer;
    };
    const toB64 = (buf) => btoa(String.fromCharCode(...new Uint8Array(buf))).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');

    const supported = () => !!(window.PublicKeyCredential && navigator.credentials && window.isSecureContext);

    function friendlyError(e) {
        if (e && e.name === 'NotAllowedError') return new Error('Passkey request was cancelled or timed out.');
        if (e && e.name === 'InvalidStateError') return new Error('This device already has a passkey for this site.');
        if (e && e.name === 'SecurityError') return new Error('Passkeys need HTTPS (or localhost) and a proper domain name.');
        return e;
    }

    async function create(options) {
        const publicKey = {
            ...options,
            challenge: toBuf(options.challenge),
            user: { ...options.user, id: toBuf(options.user.id) },
            excludeCredentials: (options.excludeCredentials || []).map((c) => ({ ...c, id: toBuf(c.id) })),
        };
        let cred;
        try { cred = await navigator.credentials.create({ publicKey }); } catch (e) { throw friendlyError(e); }
        return {
            id: cred.id,
            clientDataJSON: toB64(cred.response.clientDataJSON),
            attestationObject: toB64(cred.response.attestationObject),
        };
    }

    async function get(options) {
        const publicKey = { ...options, challenge: toBuf(options.challenge) };
        let cred;
        try { cred = await navigator.credentials.get({ publicKey }); } catch (e) { throw friendlyError(e); }
        return {
            id: cred.id,
            clientDataJSON: toB64(cred.response.clientDataJSON),
            authenticatorData: toB64(cred.response.authenticatorData),
            signature: toB64(cred.response.signature),
            userHandle: cred.response.userHandle ? toB64(cred.response.userHandle) : null,
        };
    }

    // A sensible default name for a new passkey, e.g. "Chrome on Windows".
    function deviceName() {
        const ua = navigator.userAgent;
        const os = /iPhone|iPad/.test(ua) ? 'iPhone/iPad' : /Android/.test(ua) ? 'Android' : /Mac/.test(ua) ? 'Mac'
            : /Windows/.test(ua) ? 'Windows' : /Linux/.test(ua) ? 'Linux' : 'this device';
        const br = /Edg\//.test(ua) ? 'Edge' : /Firefox\//.test(ua) ? 'Firefox' : /Chrome\//.test(ua) ? 'Chrome' : /Safari\//.test(ua) ? 'Safari' : 'Browser';
        return `${br} on ${os}`;
    }

    return { supported, create, get, deviceName };
})();
