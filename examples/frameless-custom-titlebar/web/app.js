(function () {
    'use strict';

    const state = document.getElementById('state');
    const size = document.getElementById('size');
    const focus = document.getElementById('focus');
    const titlebar = document.getElementById('titlebar');
    let nativeFullscreen = false;

    async function refresh() {
        const bounds = await flare.window.getBounds();
        size.textContent = Math.round(bounds.width) + ' x ' + Math.round(bounds.height);
        const maximized = await flare.window.isMaximized();
        const minimized = await flare.window.isMinimized();
        state.textContent = nativeFullscreen ? 'fullscreen' : (maximized ? 'maximized' : (minimized ? 'minimized' : 'normal'));
        focus.textContent = (await flare.window.isFocused()) ? 'yes' : 'no';
    }

    document.getElementById('minimize').addEventListener('click', function () {
        flare.window.minimize();
    });

    document.getElementById('maximize').addEventListener('click', async function () {
        const maximized = await flare.window.isMaximized();
        if (maximized) {
            await flare.window.restore();
        } else {
            await flare.window.maximize();
        }
        setTimeout(refresh, 120);
    });

    document.getElementById('close').addEventListener('click', function () {
        flare.window.close();
    });

    document.getElementById('center').addEventListener('click', async function () {
        await flare.window.center();
        refresh();
    });

    document.getElementById('read').addEventListener('click', refresh);

    document.getElementById('fullscreen').addEventListener('click', async function () {
        nativeFullscreen = !nativeFullscreen;
        await flare.window.setFullscreen(nativeFullscreen);
        setTimeout(refresh, 180);
    });

    titlebar.addEventListener('dblclick', async function () {
        const maximized = await flare.window.isMaximized();
        if (maximized) {
            await flare.window.restore();
        } else {
            await flare.window.maximize();
        }
    });

    window.addEventListener('resize', function () {
        refresh();
    });

    setTimeout(refresh, 200);
})();
