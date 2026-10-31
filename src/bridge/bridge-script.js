(function () {
    'use strict';

    var bootstrap = window.__flareBootstrap || {};
    var isTopFrame = true;
    try {
        isTopFrame = window.top === window;
    } catch (topError) {
        isTopFrame = false;
    }
    if (!isTopFrame && !bootstrap.allowIframes) {
        return;
    }
    if (window.flare && window.flare.__flareBridge === true) {
        return;
    }

    var channel = null;
    if (window.chrome && window.chrome.webview && typeof window.chrome.webview.postMessage === 'function') {
        channel = 'webview2';
    } else if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.flare && typeof window.webkit.messageHandlers.flare.postMessage === 'function') {
        channel = 'webkit';
    }
    if (!channel) {
        return;
    }

    var shaConstants = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    ];
    var base64Alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';

    function rotr(value, bits) {
        return ((value >>> bits) | (value << (32 - bits))) >>> 0;
    }

    function utf8Bytes(text) {
        if (window.TextEncoder) {
            return new window.TextEncoder().encode(text);
        }
        var bytes = [];
        for (var index = 0; index < text.length; index += 1) {
            var code = text.charCodeAt(index);
            if (code >= 0xD800 && code <= 0xDBFF && index + 1 < text.length) {
                var next = text.charCodeAt(index + 1);
                if (next >= 0xDC00 && next <= 0xDFFF) {
                    code = 0x10000 + ((code - 0xD800) << 10) + (next - 0xDC00);
                    index += 1;
                }
            }
            if (code < 0x80) {
                bytes.push(code);
            } else if (code < 0x800) {
                bytes.push(0xC0 | (code >> 6), 0x80 | (code & 0x3F));
            } else if (code < 0x10000) {
                bytes.push(0xE0 | (code >> 12), 0x80 | ((code >> 6) & 0x3F), 0x80 | (code & 0x3F));
            } else {
                bytes.push(0xF0 | (code >> 18), 0x80 | ((code >> 12) & 0x3F), 0x80 | ((code >> 6) & 0x3F), 0x80 | (code & 0x3F));
            }
        }
        return new Uint8Array(bytes);
    }

    function sha256(bytes) {
        var length = bytes.length;
        var paddedLength = (((length + 9 + 63) >> 6) << 6);
        var padded = new Uint8Array(paddedLength);
        padded.set(bytes);
        padded[length] = 0x80;
        var bitLengthHigh = Math.floor(length / 536870912);
        var bitLengthLow = (length << 3) >>> 0;
        padded[paddedLength - 8] = (bitLengthHigh >>> 24) & 0xFF;
        padded[paddedLength - 7] = (bitLengthHigh >>> 16) & 0xFF;
        padded[paddedLength - 6] = (bitLengthHigh >>> 8) & 0xFF;
        padded[paddedLength - 5] = bitLengthHigh & 0xFF;
        padded[paddedLength - 4] = (bitLengthLow >>> 24) & 0xFF;
        padded[paddedLength - 3] = (bitLengthLow >>> 16) & 0xFF;
        padded[paddedLength - 2] = (bitLengthLow >>> 8) & 0xFF;
        padded[paddedLength - 1] = bitLengthLow & 0xFF;

        var hash = [0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19];
        var schedule = new Uint32Array(64);

        for (var offset = 0; offset < paddedLength; offset += 64) {
            for (var word = 0; word < 16; word += 1) {
                var position = offset + word * 4;
                schedule[word] = ((padded[position] << 24) | (padded[position + 1] << 16) | (padded[position + 2] << 8) | padded[position + 3]) >>> 0;
            }
            for (var extended = 16; extended < 64; extended += 1) {
                var s0 = (rotr(schedule[extended - 15], 7) ^ rotr(schedule[extended - 15], 18) ^ (schedule[extended - 15] >>> 3)) >>> 0;
                var s1 = (rotr(schedule[extended - 2], 17) ^ rotr(schedule[extended - 2], 19) ^ (schedule[extended - 2] >>> 10)) >>> 0;
                schedule[extended] = (schedule[extended - 16] + s0 + schedule[extended - 7] + s1) >>> 0;
            }

            var a = hash[0];
            var b = hash[1];
            var c = hash[2];
            var d = hash[3];
            var e = hash[4];
            var f = hash[5];
            var g = hash[6];
            var h = hash[7];

            for (var round = 0; round < 64; round += 1) {
                var sum1 = (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) >>> 0;
                var choice = ((e & f) ^ ((~e) & g)) >>> 0;
                var temp1 = (h + sum1 + choice + shaConstants[round] + schedule[round]) >>> 0;
                var sum0 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) >>> 0;
                var majority = ((a & b) ^ (a & c) ^ (b & c)) >>> 0;
                var temp2 = (sum0 + majority) >>> 0;
                h = g;
                g = f;
                f = e;
                e = (d + temp1) >>> 0;
                d = c;
                c = b;
                b = a;
                a = (temp1 + temp2) >>> 0;
            }

            hash[0] = (hash[0] + a) >>> 0;
            hash[1] = (hash[1] + b) >>> 0;
            hash[2] = (hash[2] + c) >>> 0;
            hash[3] = (hash[3] + d) >>> 0;
            hash[4] = (hash[4] + e) >>> 0;
            hash[5] = (hash[5] + f) >>> 0;
            hash[6] = (hash[6] + g) >>> 0;
            hash[7] = (hash[7] + h) >>> 0;
        }

        var digest = new Uint8Array(32);
        for (var index = 0; index < 8; index += 1) {
            digest[index * 4] = (hash[index] >>> 24) & 0xFF;
            digest[index * 4 + 1] = (hash[index] >>> 16) & 0xFF;
            digest[index * 4 + 2] = (hash[index] >>> 8) & 0xFF;
            digest[index * 4 + 3] = hash[index] & 0xFF;
        }
        return digest;
    }

    function hmacSha256(keyBytes, messageBytes) {
        var block = new Uint8Array(64);
        if (keyBytes.length > 64) {
            block.set(sha256(keyBytes));
        } else {
            block.set(keyBytes);
        }
        var inner = new Uint8Array(64 + messageBytes.length);
        var outer = new Uint8Array(64 + 32);
        for (var index = 0; index < 64; index += 1) {
            inner[index] = block[index] ^ 0x36;
            outer[index] = block[index] ^ 0x5C;
        }
        inner.set(messageBytes, 64);
        outer.set(sha256(inner), 64);
        return sha256(outer);
    }

    function base64Url(bytes) {
        var output = '';
        var index = 0;
        while (index + 3 <= bytes.length) {
            var value = (bytes[index] << 16) | (bytes[index + 1] << 8) | bytes[index + 2];
            output += base64Alphabet[(value >>> 18) & 0x3F];
            output += base64Alphabet[(value >>> 12) & 0x3F];
            output += base64Alphabet[(value >>> 6) & 0x3F];
            output += base64Alphabet[value & 0x3F];
            index += 3;
        }
        var remaining = bytes.length - index;
        if (remaining === 1) {
            var single = bytes[index] << 16;
            output += base64Alphabet[(single >>> 18) & 0x3F];
            output += base64Alphabet[(single >>> 12) & 0x3F];
        } else if (remaining === 2) {
            var pair = (bytes[index] << 16) | (bytes[index + 1] << 8);
            output += base64Alphabet[(pair >>> 18) & 0x3F];
            output += base64Alphabet[(pair >>> 12) & 0x3F];
            output += base64Alphabet[(pair >>> 6) & 0x3F];
        }
        return output;
    }

    function randomBytes(count) {
        var bytes = new Uint8Array(count);
        if (window.crypto && typeof window.crypto.getRandomValues === 'function') {
            window.crypto.getRandomValues(bytes);
            return bytes;
        }
        var seed = Date.now() ^ Math.floor(Math.random() * 0xFFFFFFFF);
        for (var index = 0; index < count; index += 1) {
            seed = (seed * 1664525 + 1013904223) >>> 0;
            bytes[index] = (seed >>> 24) & 0xFF;
        }
        return bytes;
    }

    function bytesToHex(bytes) {
        var text = '';
        for (var index = 0; index < bytes.length; index += 1) {
            var value = bytes[index];
            text += (value < 16 ? '0' : '') + value.toString(16);
        }
        return text;
    }

    function post(message) {
        try {
            if (channel === 'webview2') {
                window.chrome.webview.postMessage(message);
            } else {
                window.webkit.messageHandlers.flare.postMessage(message);
            }
            return true;
        } catch (postError) {
            return false;
        }
    }

    var session = null;
    var sequence = 0;
    var requestCounter = 0;
    var outbox = [];
    var pendingRequests = Object.create(null);
    var listeners = Object.create(null);
    var helloAttempts = 0;
    var queueTimer = null;
    var allowedActions = null;
    var dragRegions = Array.isArray(bootstrap.draggableRegions) ? bootstrap.draggableRegions.slice() : [];
    var dragInstalled = false;
    var appInfo = {
        name: bootstrap.appName || 'Flare Application',
        version: bootstrap.appVersion || '',
        flareVersion: bootstrap.flareVersion || '1.0.0',
        platform: bootstrap.platform || 'unknown'
    };
    var limits = {
        maxMessageBytes: bootstrap.maxMessageBytes || 1048576,
        maxPayloadBytes: bootstrap.maxPayloadBytes || 524288,
        invokeTimeoutMs: bootstrap.invokeTimeoutMs || 15000
    };

    function isValidName(name) {
        return typeof name === 'string' && name.length > 0 && name.length <= 128 && /^[A-Za-z0-9_][A-Za-z0-9_.:-]*$/.test(name);
    }

    function makeError(code, message) {
        var error = new Error(message);
        error.code = code;
        return error;
    }

    function reportCallbackError(error) {
        if (window.console && typeof window.console.error === 'function') {
            window.console.error(error);
        }
    }

    function rejectAllQueued(code, message) {
        var queued = outbox;
        outbox = [];
        for (var index = 0; index < queued.length; index += 1) {
            var entry = queued[index];
            if (typeof entry.reject === 'function') {
                entry.reject(makeError(code, message));
            }
        }
        for (var key in pendingRequests) {
            if (Object.prototype.hasOwnProperty.call(pendingRequests, key)) {
                var request = pendingRequests[key];
                if (request.timer) {
                    clearTimeout(request.timer);
                }
                request.reject(makeError(code, message));
                delete pendingRequests[key];
            }
        }
    }

    function ensureQueueTimer() {
        if (queueTimer || session) {
            return;
        }
        var timeout = bootstrap.handshakeTimeoutMs || 10000;
        queueTimer = setTimeout(function () {
            queueTimer = null;
            if (!session) {
                rejectAllQueued('timeout', 'Bridge handshake timed out');
            }
        }, timeout);
    }

    function nextRequestId() {
        requestCounter += 1;
        return 'req-' + requestCounter + '-' + bytesToHex(randomBytes(6));
    }

    function sendNow(operation) {
        if (!session) {
            outbox.push(operation);
            ensureQueueTimer();
            return;
        }
        if (operation.type === 'windowAction' && allowedActions && allowedActions.indexOf(operation.name) === -1) {
            if (typeof operation.reject === 'function') {
                operation.reject(makeError('permission_denied', 'Window action is not permitted for this window'));
            }
            return;
        }
        if (operation.payload.length > limits.maxPayloadBytes) {
            if (typeof operation.reject === 'function') {
                operation.reject(makeError('invalid_argument', 'Bridge payload exceeds the size limit'));
            }
            return;
        }
        sequence += 1;
        var timestamp = Date.now();
        var nonce = bytesToHex(randomBytes(16));
        var canonical = 'v1|' + session.sid + '|' + sequence + '|' + timestamp + '|' + nonce + '|' + operation.type + '|' + operation.name + '|' + operation.id + '|' + operation.payload;
        var mac = base64Url(hmacSha256(utf8Bytes(session.token), utf8Bytes(canonical)));
        var envelope = {
            flare: 1,
            v: 1,
            type: operation.type,
            sid: session.sid,
            seq: sequence,
            ts: timestamp,
            nonce: nonce,
            name: operation.name,
            id: operation.id,
            payload: operation.payload,
            mac: mac
        };
        if (!post(envelope)) {
            if (typeof operation.reject === 'function') {
                operation.reject(makeError('internal', 'Unable to post to the native bridge'));
            }
            return;
        }
        if (typeof operation.resolve === 'function' && operation.id) {
            var requestId = operation.id;
            var request = {
                resolve: operation.resolve,
                reject: operation.reject,
                timer: null
            };
            request.timer = setTimeout(function () {
                if (pendingRequests[requestId]) {
                    delete pendingRequests[requestId];
                    operation.reject(makeError('timeout', 'Native bridge invocation timed out'));
                }
            }, limits.invokeTimeoutMs);
            pendingRequests[requestId] = request;
        }
    }

    function enqueue(operation) {
        sendNow(operation);
    }

    function acceptHandshake(message) {
        if (!message || typeof message.sid !== 'string' || typeof message.token !== 'string') {
            return;
        }
        if (session && session.sid === message.sid) {
            if (message.limits && typeof message.limits === 'object') {
                limits = message.limits;
            }
            if (Array.isArray(message.actions)) {
                allowedActions = message.actions.slice();
            }
            if (message.config && typeof message.config === 'object') {
                applyConfig(message.config);
            }
            flushOutbox();
            return;
        }
        session = {
            sid: message.sid,
            token: message.token
        };
        sequence = 0;
        if (message.limits && typeof message.limits === 'object') {
            limits = message.limits;
        }
        if (Array.isArray(message.actions)) {
            allowedActions = message.actions.slice();
        } else {
            allowedActions = null;
        }
        if (message.app && typeof message.app === 'object') {
            appInfo.name = message.app.name || appInfo.name;
            appInfo.version = message.app.version || appInfo.version;
            appInfo.flareVersion = message.app.flareVersion || appInfo.flareVersion;
            appInfo.platform = message.app.platform || appInfo.platform;
        }
        if (message.config && typeof message.config === 'object') {
            applyConfig(message.config);
        }
        if (queueTimer) {
            clearTimeout(queueTimer);
            queueTimer = null;
        }
        flushOutbox();
    }

    function flushOutbox() {
        var queued = outbox;
        outbox = [];
        for (var index = 0; index < queued.length; index += 1) {
            sendNow(queued[index]);
        }
    }

    function applyConfig(config) {
        if (Array.isArray(config.draggableRegions)) {
            dragRegions = config.draggableRegions.slice();
            installDragRegions();
        }
    }

    function dispatchEvent(name, payload) {
        var entries = listeners[name];
        if (!entries || entries.length === 0) {
            return;
        }
        var snapshot = entries.slice();
        for (var index = 0; index < snapshot.length; index += 1) {
            var entry = snapshot[index];
            if (entry.once) {
                var current = listeners[name];
                if (current) {
                    var position = current.indexOf(entry);
                    if (position >= 0) {
                        current.splice(position, 1);
                    }
                }
            }
            try {
                entry.handler(payload);
            } catch (handlerError) {
                setTimeout(function () {
                    throw handlerError;
                }, 0);
            }
        }
    }

    function receive(message) {
        if (typeof message === 'string') {
            try {
                message = JSON.parse(message);
            } catch (parseError) {
                return;
            }
        }
        if (!message || typeof message !== 'object') {
            return;
        }
        if (message.type === 'handshake') {
            acceptHandshake(message);
            return;
        }
        if (message.type === 'event') {
            if (typeof message.name === 'string') {
                dispatchEvent(message.name, message.payload);
            }
            return;
        }
        if (message.type === 'result' || message.type === 'error') {
            var requestId = message.id;
            if (typeof requestId !== 'string') {
                return;
            }
            var request = pendingRequests[requestId];
            if (!request) {
                return;
            }
            delete pendingRequests[requestId];
            if (request.timer) {
                clearTimeout(request.timer);
            }
            if (message.type === 'result') {
                request.resolve(message.payload);
            } else {
                request.reject(makeError(message.code || 'internal', message.message || 'Native bridge invocation failed'));
            }
            return;
        }
        if (message.type === 'config') {
            if (message.config && typeof message.config === 'object') {
                applyConfig(message.config);
            }
        }
    }

    function windowAction(name, args) {
        return new Promise(function (resolve, reject) {
            var payload;
            try {
                payload = JSON.stringify(args === undefined ? {} : args);
            } catch (serializationError) {
                reject(makeError('invalid_argument', 'Window action payload is not JSON serializable'));
                return;
            }
            if (typeof payload !== 'string') {
                reject(makeError('invalid_argument', 'Window action payload is not JSON serializable'));
                return;
            }
            enqueue({
                type: 'windowAction',
                name: name,
                id: nextRequestId(),
                payload: payload,
                resolve: resolve,
                reject: reject
            });
        });
    }

    function invoke(name, args) {
        return new Promise(function (resolve, reject) {
            if (!isValidName(name)) {
                reject(makeError('invalid_argument', 'Handler name is invalid'));
                return;
            }
            var payload;
            try {
                payload = JSON.stringify(args === undefined ? {} : args);
            } catch (serializationError) {
                reject(makeError('invalid_argument', 'Invocation payload is not JSON serializable'));
                return;
            }
            if (typeof payload !== 'string') {
                reject(makeError('invalid_argument', 'Invocation payload is not JSON serializable'));
                return;
            }
            enqueue({
                type: 'invoke',
                name: name,
                id: nextRequestId(),
                payload: payload,
                resolve: resolve,
                reject: reject
            });
        });
    }

    function on(name, handler) {
        if (!isValidName(name)) {
            throw new TypeError('Event name is invalid');
        }
        if (typeof handler !== 'function') {
            throw new TypeError('Event handler must be a function');
        }
        if (!listeners[name]) {
            listeners[name] = [];
        }
        var entry = {handler: handler, once: false};
        listeners[name].push(entry);
        return function () {
            var entries = listeners[name];
            if (!entries) {
                return;
            }
            var position = entries.indexOf(entry);
            if (position >= 0) {
                entries.splice(position, 1);
            }
        };
    }

    function once(name, handler) {
        if (!isValidName(name)) {
            throw new TypeError('Event name is invalid');
        }
        if (typeof handler !== 'function') {
            throw new TypeError('Event handler must be a function');
        }
        if (!listeners[name]) {
            listeners[name] = [];
        }
        var entry = {handler: handler, once: true};
        listeners[name].push(entry);
        return function () {
            var entries = listeners[name];
            if (!entries) {
                return;
            }
            var position = entries.indexOf(entry);
            if (position >= 0) {
                entries.splice(position, 1);
            }
        };
    }

    function off(name, handler) {
        if (!isValidName(name)) {
            throw new TypeError('Event name is invalid');
        }
        var entries = listeners[name];
        if (!entries) {
            return;
        }
        if (typeof handler !== 'function') {
            delete listeners[name];
            return;
        }
        for (var index = entries.length - 1; index >= 0; index -= 1) {
            if (entries[index].handler === handler) {
                entries.splice(index, 1);
            }
        }
        if (entries.length === 0) {
            delete listeners[name];
        }
    }

    function emit(name, args) {
        if (!isValidName(name)) {
            throw new TypeError('Event name is invalid');
        }
        var payload;
        try {
            payload = JSON.stringify(args === undefined ? {} : args);
        } catch (serializationError) {
            throw new TypeError('Event payload is not JSON serializable');
        }
        if (typeof payload !== 'string') {
            throw new TypeError('Event payload is not JSON serializable');
        }
        enqueue({
            type: 'emit',
            name: name,
            id: '',
            payload: payload
        });
    }

    function isInteractiveTarget(target) {
        if (!target || typeof target.closest !== 'function') {
            return false;
        }
        return target.closest('input,textarea,select,button,a,[contenteditable=""],[contenteditable="true"],[data-flare-no-drag]') !== null;
    }

    function isAppRegionDrag(target) {
        var element = target;
        while (element && element.nodeType === 1) {
            var style = window.getComputedStyle(element);
            var region = style.getPropertyValue('-webkit-app-region') || style.webkitAppRegion || style.appRegion;
            region = String(region || '').trim().toLowerCase();
            if (region === 'no-drag') {
                return false;
            }
            if (region === 'drag') {
                return true;
            }
            element = element.parentElement;
        }
        return false;
    }

    function installDragRegions() {
        if (dragInstalled) {
            return;
        }
        dragInstalled = true;
        document.addEventListener('mousedown', function (event) {
            if (event.button !== 0 || event.defaultPrevented) {
                return;
            }
            if (isInteractiveTarget(event.target)) {
                return;
            }
            for (var index = 0; index < dragRegions.length; index += 1) {
                var region = dragRegions[index];
                if (!region || typeof region.x !== 'number' || typeof region.y !== 'number' || typeof region.width !== 'number' || typeof region.height !== 'number') {
                    continue;
                }
                if (event.clientX >= region.x && event.clientX < region.x + region.width && event.clientY >= region.y && event.clientY < region.y + region.height) {
                    event.preventDefault();
                    windowAction('startDrag', {}).catch(function () {});
                    return;
                }
            }
            if (isAppRegionDrag(event.target)) {
                event.preventDefault();
                windowAction('startDrag', {}).catch(function () {});
            }
        }, true);
    }

    window.__flareReceive = receive;
    if (channel === 'webview2') {
        window.chrome.webview.addEventListener('message', function (event) {
            receive(event.data);
        });
    }
    installDragRegions();

    var flare = {
        version: '1.0.0',
        __flareBridge: true,
        on: on,
        off: off,
        once: once,
        emit: emit,
        invoke: invoke,
        app: appInfo,
        window: {
            show: function () { return windowAction('show', {}); },
            hide: function () { return windowAction('hide', {}); },
            minimize: function () { return windowAction('minimize', {}); },
            maximize: function () { return windowAction('maximize', {}); },
            restore: function () { return windowAction('restore', {}); },
            close: function () { return windowAction('close', {}); },
            focus: function () { return windowAction('focus', {}); },
            setTitle: function (title) { return windowAction('setTitle', {title: String(title)}); },
            setSize: function (width, height) { return windowAction('setSize', {width: Number(width), height: Number(height)}); },
            setPosition: function (x, y) { return windowAction('setPosition', {x: Number(x), y: Number(y)}); },
            setOpacity: function (opacity) { return windowAction('setOpacity', {opacity: Number(opacity)}); },
            setFullscreen: function (fullscreen) { return windowAction('setFullscreen', {fullscreen: Boolean(fullscreen)}); },
            setAlwaysOnTop: function (alwaysOnTop) { return windowAction('setAlwaysOnTop', {alwaysOnTop: Boolean(alwaysOnTop)}); },
            center: function () { return windowAction('center', {}); },
            getBounds: function () { return windowAction('getBounds', {}); },
            isMaximized: function () { return windowAction('isMaximized', {}); },
            isMinimized: function () { return windowAction('isMinimized', {}); },
            isVisible: function () { return windowAction('isVisible', {}); },
            isFocused: function () { return windowAction('isFocused', {}); },
            startDrag: function () { return windowAction('startDrag', {}); }
        }
    };

    window.flare = flare;

    function sayHello() {
        if (session) {
            return;
        }
        helloAttempts += 1;
        if (helloAttempts > 20) {
            rejectAllQueued('timeout', 'Bridge handshake timed out');
            return;
        }
        post({
            flare: 1,
            v: 1,
            type: 'hello',
            nonce: bytesToHex(randomBytes(16))
        });
        setTimeout(sayHello, 500);
    }

    sayHello();
})();
