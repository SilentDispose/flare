(function () {
    'use strict';

    const connection = document.getElementById('connection');
    const feed = document.getElementById('feed');
    const addResult = document.getElementById('add-result');
    const eventResult = document.getElementById('event-result');
    const windowResult = document.getElementById('window-result');
    const profileResult = document.getElementById('profile-result');

    function appendFeed(text) {
        const row = document.createElement('div');
        row.textContent = text;
        feed.prepend(row);
    }

    function showError(target, error) {
        target.textContent = error.code + ': ' + error.message;
    }

    flare.on('welcome', function (payload) {
        connection.textContent = 'connected';
        connection.classList.add('ready');
        appendFeed('Native welcome: ' + payload.message + ' (protocol ' + payload.protocol + ')');
    });

    flare.on('button-clicked-ack', function (payload) {
        appendFeed('Native acknowledged button-clicked: ' + JSON.stringify(payload.received));
    });

    appendFeed('Bridge client for ' + flare.app.name + ' ' + flare.app.version + ' on ' + flare.app.platform);

    document.getElementById('add').addEventListener('click', async function () {
        try {
            const result = await flare.invoke('add', {
                left: Number(document.getElementById('left').value),
                right: Number(document.getElementById('right').value)
            });
            addResult.textContent = 'C++ returned sum = ' + result.sum;
        } catch (error) {
            showError(addResult, error);
        }
    });

    document.getElementById('emit').addEventListener('click', function () {
        flare.emit('button-clicked', {at: Date.now(), label: 'demo button'});
        eventResult.textContent = 'Custom event sent to C++.';
    });

    document.getElementById('secret').addEventListener('click', async function () {
        try {
            const value = await flare.invoke('secret');
            eventResult.textContent = JSON.stringify(value);
        } catch (error) {
            eventResult.textContent = 'Rejected as expected: ' + error.code + ' - ' + error.message;
        }
    });

    document.getElementById('profile').addEventListener('click', async function () {
        try {
            const profile = await flare.invoke('profile');
            profileResult.textContent = JSON.stringify(profile, null, 2);
        } catch (error) {
            profileResult.textContent = error.code + ': ' + error.message;
        }
    });

    document.getElementById('title').addEventListener('click', async function () {
        await flare.window.setTitle('Bridge Demo ' + new Date().toLocaleTimeString());
        windowResult.textContent = 'Native title updated.';
    });

    document.getElementById('bounds').addEventListener('click', async function () {
        const bounds = await flare.window.getBounds();
        windowResult.textContent = 'x=' + Math.round(bounds.x) + ' y=' + Math.round(bounds.y) + ' width=' + Math.round(bounds.width) + ' height=' + Math.round(bounds.height);
    });

    document.getElementById('center').addEventListener('click', async function () {
        await flare.window.center();
        windowResult.textContent = 'Window centered.';
    });

    let alwaysOnTop = false;
    document.getElementById('top').addEventListener('click', async function () {
        alwaysOnTop = !alwaysOnTop;
        await flare.window.setAlwaysOnTop(alwaysOnTop);
        windowResult.textContent = alwaysOnTop ? 'Window is always on top.' : 'Window returned to normal level.';
    });

    flare.invoke('echo', {ready: true}).then(function (value) {
        appendFeed('Echo handler replied: ' + JSON.stringify(value.echo));
    }).catch(function (error) {
        appendFeed('Echo failed: ' + error.message);
    });
})();
