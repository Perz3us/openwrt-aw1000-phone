'use strict';
'require view';
'require rpc';
'require poll';
'require ui';
'require dom';

var callStatus = rpc.declare({ object: 'fxs', method: 'status' });
var callClearGuard = rpc.declare({ object: 'fxs', method: 'clear_guard' });
var callSetEnabled = rpc.declare({ object: 'fxs', method: 'set_enabled', params: [ 'enabled' ] });
var callRingTest = rpc.declare({ object: 'fxs', method: 'ring_test' });

var stateText = {
	idle: _('Idle (on hook)'),
	dialtone: _('Dial tone'),
	digits: _('Dialing'),
	calling: _('Calling'),
	talking: _('In call'),
	ringing: _('Ringing'),
	busy: _('Busy tone'),
	waiting_modem: _('Waiting for modem'),
	modem_master: _('Modem audio not set to slave mode'),
	modem_restart: _('Restarting modem'),
	stopped: _('Stopped')
};

var css = '\
.fxs-top { display: flex; flex-wrap: wrap; gap: 24px; align-items: center; }\
.fxs-phone { width: 220px; text-align: center; }\
.fxs-phone svg { width: 200px; height: 170px; overflow: visible; }\
.fxs-phone .body { fill: #3b4048; stroke: #8b919b; stroke-width: 2; }\
.fxs-phone .key { fill: #8b919b; }\
.fxs-phone .hook { fill: #8b919b; }\
.fxs-phone .cord { fill: none; stroke: #8b919b; stroke-width: 2.5; stroke-dasharray: 3 3; }\
.fxs-phone .handset { fill: #555b64; stroke: #a7adb6; stroke-width: 2; transition: transform .45s cubic-bezier(.3,1.4,.5,1), fill .3s; transform-origin: 100px 52px; }\
.fxs-phone.offhook .handset { transform: translate(-6px, -34px) rotate(-14deg); }\
.fxs-phone.dialtone .handset, .fxs-phone.digits .handset, .fxs-phone.calling .handset { fill: #c58f12; }\
.fxs-phone.talking .handset { fill: #2e8b3e; }\
.fxs-phone.busy .handset { fill: #c0392b; }\
.fxs-phone .waves path { fill: none; stroke: #e0a800; stroke-width: 3; stroke-linecap: round; opacity: 0; }\
.fxs-phone.ringing .waves path { animation: fxs-wave 1s infinite; }\
.fxs-phone.ringing .waves path:nth-child(2) { animation-delay: .2s; }\
.fxs-phone.ringing .shake { animation: fxs-shake .18s infinite; transform-origin: 100px 120px; }\
.fxs-phone.talking .dot { animation: fxs-pulse 1.2s infinite; }\
.fxs-phone .dot { fill: #2e8b3e; opacity: 0; }\
.fxs-phone.stopped svg { opacity: .3; filter: grayscale(1); }\
.fxs-phone .fxs-state { display: inline-flex; align-items: center; gap: 8px; margin-top: 10px; padding: 5px 16px; border-radius: 999px; background: rgba(255,255,255,.07); font-size: .95em; font-weight: 600; letter-spacing: .2px; }\
.fxs-phone .fxs-state::before { content: ""; width: 9px; height: 9px; border-radius: 50%; background: #8b919b; }\
.fxs-phone.idle .fxs-state::before { background: #3fb950; }\
.fxs-phone.dialtone .fxs-state::before, .fxs-phone.digits .fxs-state::before, .fxs-phone.calling .fxs-state::before { background: #e0a800; }\
.fxs-phone.ringing .fxs-state::before { background: #e0a800; animation: fxs-blink 1s infinite; }\
.fxs-phone.talking .fxs-state::before { background: #3fb950; animation: fxs-blink 1.2s infinite; }\
.fxs-phone.busy .fxs-state::before { background: #e5534b; }\
.fxs-phone.disabled .fxs-state::before, .fxs-phone.stopped .fxs-state::before, .fxs-phone.waiting_modem .fxs-state::before, .fxs-phone.modem_restart .fxs-state::before { background: #8b919b; animation: none; }\
.fxs-phone.modem_master .fxs-state::before { background: #e5534b; }\
.fxs-phone.disabled svg, .fxs-phone.waiting_modem svg, .fxs-phone.modem_master svg, .fxs-phone.modem_restart svg { opacity: .3; filter: grayscale(1); }\
.fxs-phone .fxs-peer { margin-top: 6px; opacity: .75; min-height: 1.2em; font-variant-numeric: tabular-nums; }\
.fxs-phone .fxs-timer { font-size: 1.6em; font-weight: bold; color: #2e8b3e; min-height: 1.2em; font-variant-numeric: tabular-nums; }\
@keyframes fxs-blink { 0%,100% { opacity: 1; } 50% { opacity: .25; } }\
.fxs-switch { min-width: 170px; font-size: 1.05em; }\
@keyframes fxs-shake { 0%,100% { transform: rotate(0); } 25% { transform: rotate(-4deg); } 75% { transform: rotate(4deg); } }\
@keyframes fxs-wave { 0% { opacity: 0; } 40% { opacity: 1; } 100% { opacity: 0; } }\
@keyframes fxs-pulse { 0%,100% { opacity: 0; } 50% { opacity: 1; } }\
';

var phoneSvg = '\
<svg viewBox="0 0 200 170" xmlns="http://www.w3.org/2000/svg">\
<g class="shake">\
<path class="cord" d="M36 66 C 8 92, 14 132, 40 150"/>\
<path class="body" d="M44 84 Q48 66 72 66 L128 66 Q152 66 156 84 L168 146 Q170 158 156 158 L44 158 Q30 158 32 146 Z"/>\
<rect class="hook" x="46" y="58" width="16" height="10" rx="3"/>\
<rect class="hook" x="138" y="58" width="16" height="10" rx="3"/>\
<g>\
<circle class="key" cx="82" cy="96" r="4"/><circle class="key" cx="100" cy="96" r="4"/><circle class="key" cx="118" cy="96" r="4"/>\
<circle class="key" cx="82" cy="110" r="4"/><circle class="key" cx="100" cy="110" r="4"/><circle class="key" cx="118" cy="110" r="4"/>\
<circle class="key" cx="82" cy="124" r="4"/><circle class="key" cx="100" cy="124" r="4"/><circle class="key" cx="118" cy="124" r="4"/>\
<circle class="key" cx="82" cy="138" r="4"/><circle class="key" cx="100" cy="138" r="4"/><circle class="key" cx="118" cy="138" r="4"/>\
</g>\
<circle class="dot" cx="148" cy="146" r="5"/>\
<path class="handset" d="M26 52 Q26 30 50 30 L150 30 Q174 30 174 52 L174 58 Q174 66 164 66 L142 66 Q132 66 132 56 L132 48 L68 48 L68 56 Q68 66 58 66 L36 66 Q26 66 26 58 Z"/>\
</g>\
<g class="waves">\
<path d="M178 18 q12 12 0 26"/><path d="M188 10 q20 20 0 42"/>\
<path d="M22 18 q-12 12 0 26"/><path d="M12 10 q-20 20 0 42"/>\
</g>\
</svg>';

function yesno(v) {
	return v ? _('yes') : _('no');
}

function volts(mv) {
	return '%.1f V'.format(Math.abs(mv) / 1000);
}

function milliamps(ua) {
	return '%.1f mA'.format(Math.abs(ua) / 1000);
}

function lineRows(st) {
	var v = Math.abs(st.vline || 0) / 1000, i = Math.abs(st.iloop || 0) / 1000, b = Math.abs(st.vbat || 0) / 1000;
	var ringing = st.state === 'ringing', rows = [], line, cur, bat;

	if (!st.running || st.vbat == null)
		return [ E('tr', { 'class': 'tr placeholder' }, [
			E('td', { 'class': 'td' }, E('em', _('Available while the phone port is on')))
		]) ];

	if (ringing) {
		line = cur = bat = _('Ringing, measured values are not meaningful');
	} else if (st.offhook) {
		line = v > 3 && v < 15 ? _('OK') : _('Unusual for a lifted handset');
		cur = i >= 15 && i <= 30 ? _('OK') : (i < 15 ? _('Low, check the cable and the phone') : _('High'));
		bat = _('OK, it drops while the handset is lifted');
	} else {
		line = v >= 40 && v <= 55 ? _('OK') : (v < 30 ? _('Low, check for a short or another device on the line') : _('Unusual'));
		cur = i < 3 ? _('OK') : _('Current flows with the handset down, check the cable');
		bat = b >= 50 && b <= 70 ? _('OK') : _('Out of range');
	}

	rows.push([ _('Line voltage'), volts(st.vline), line ]);
	rows.push([ _('Loop current'), milliamps(st.iloop), cur ]);
	rows.push([ _('Battery voltage'), volts(st.vbat), bat ]);

	return rows.map(function(r) {
		return E('tr', { 'class': 'tr' }, [
			E('td', { 'class': 'td left', 'width': '33%' }, r[0]),
			E('td', { 'class': 'td left', 'width': '20%' }, r[1]),
			E('td', { 'class': 'td left' }, r[2])
		]);
	});
}

function statusRows(st) {
	var rows = [
		[ _('Line'), stateText[st.state] || st.state ],
		[ _('My number'), st.own || _('unknown, set it in Settings') ],
		[ _('Handset'), st.offhook ? _('off hook') : _('on hook') ],
		[ _('Audio'), !st.running ? '-' : (st.hd ? _('HD (16 kHz)') : _('Normal (8 kHz)')) ],
		[ _('Number'), [ 'ringing', 'calling', 'talking' ].indexOf(st.state) >= 0 && st.peer ? st.peer : '-' ],
		[ _('Service running'), yesno(st.running) ],
		[ _('Kernel module loaded'), yesno(st.module) ],
		[ _('SLIC patch present'), yesno(st.patch) ]
	];

	return rows.map(function(r) {
		return E('tr', { 'class': 'tr' }, [
			E('td', { 'class': 'td left', 'width': '33%' }, r[0]),
			E('td', { 'class': 'td left' }, r[1])
		]);
	});
}

function fmtDuration(s) {
	var h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60;

	s = s % 60;
	return (h ? h + ':' + (m < 10 ? '0' : '') : '') + m + ':' + (s < 10 ? '0' : '') + s;
}

function phoneState(st) {
	if (!st.running)
		return 'stopped';
	if (st.testring)
		return 'ringing';
	return st.state || 'idle';
}

return view.extend({
	load: function() {
		return callStatus();
	},

	updatePhone: function(st) {
		var s = phoneState(st);
		var off = st.running && (st.offhook || [ 'dialtone', 'digits', 'calling', 'talking', 'busy' ].indexOf(s) >= 0);

		this.phone.className = 'fxs-phone ' + s + (off ? ' offhook' : '') + (st.enabled === false ? ' disabled' : '');
		this.phoneLabel.textContent = st.enabled === false ? _('Phone port off') : (stateText[s] || s);
		this.phonePeer.textContent = (s === 'ringing' || s === 'calling' || s === 'talking') && st.peer ? st.peer : '';
		this.phoneTimer.textContent = s === 'talking' && st.duration != null ? fmtDuration(st.duration) : '';

		this.toggle.textContent = st.enabled === false ? _('Phone port: OFF') : _('Phone port: ON');
		this.toggle.className = 'btn fxs-switch ' + (st.enabled === false ? 'cbi-button-negative' : 'cbi-button-positive');

		if (this.ringBtn) {
			this.ringBtn.textContent = st.testring ? _('Stop ringing') : _('Ring the phone');
			this.ringBtn.className = 'btn ' + (st.testring ? 'cbi-button-negative' : 'cbi-button-action');
			this.ringBtn.disabled = !st.running || (!st.testring && (st.state !== 'idle' || st.offhook));
		}
		this.enabled = st.enabled !== false;
	},

	renderGuard: function(st) {
		if (st.running && st.voiceoff)
			return E('div', { 'class': 'alert-message warning' }, [
				E('p', _('Voice calls are turned off in the modem, so every call will fail. Run this on the router and restart the modem:')),
				E('pre', 'ubus call at-daemon sendat \'{"at_port":"/dev/ttyUSB2","at_cmd":"AT+QCFG=\\"call_control\\",0,0","timeout":3}\'')
			]);
		if (!st.guard || st.running)
			return E('div');

		return E('div', { 'class': 'alert-message warning' }, [
			E('p', _('The phone service did not finish starting last time (the router restarted), so it was not started again.')),
			E('button', {
				'class': 'btn cbi-button-action',
				'click': ui.createHandlerFn(this, function() {
					return callClearGuard().then(function() { window.location.reload(); });
				})
			}, _('Try again'))
		]);
	},

	handleRingTest: function() {
		return callRingTest().then(function(r) {
			if (!r || !r.result)
				ui.addNotification(null, E('p', _('The phone service is not running.')));
			return new Promise(function(resolve) { window.setTimeout(resolve, 300); });
		}).then(callStatus).then(L.bind(function(st) {
			this.updatePhone(st || {});
		}, this));
	},

	handleToggle: function() {
		var want = !this.enabled;

		this.toggle.disabled = true;
		this.toggle.textContent = want ? _('Starting…') : _('Stopping…');
		return callSetEnabled(want).then(L.bind(function() {
			return callStatus();
		}, this)).then(L.bind(function(st) {
			this.updatePhone(st || {});
			this.toggle.disabled = false;
		}, this)).catch(L.bind(function(e) {
			this.toggle.disabled = false;
			ui.addNotification(null, E('p', _('Could not change the phone port: %s').format(e.message)));
		}, this));
	},

	render: function(data) {
		var st = data || {};

		var statusTable = E('table', { 'class': 'table' }, statusRows(st));
		var lineTable = E('table', { 'class': 'table' }, lineRows(st));
		var guard = E('div', {}, this.renderGuard(st));

		this.phone = E('div', { 'class': 'fxs-phone' });
		this.phone.innerHTML = phoneSvg;
		this.phoneLabel = E('div', { 'class': 'fxs-state' });
		this.phonePeer = E('div', { 'class': 'fxs-peer' });
		this.phoneTimer = E('div', { 'class': 'fxs-timer' });
		this.phone.appendChild(this.phoneLabel);
		this.phone.appendChild(this.phonePeer);
		this.phone.appendChild(this.phoneTimer);
		this.toggle = E('button', { 'click': ui.createHandlerFn(this, 'handleToggle') });
		this.ringBtn = E('button', { 'click': ui.createHandlerFn(this, 'handleRingTest') });
		this.updatePhone(st);

		poll.add(L.bind(function() {
			return callStatus().then(L.bind(function(r) {
				var cur = r || {};

				this.updatePhone(cur);
				dom.content(statusTable, statusRows(cur));
				dom.content(lineTable, lineRows(cur));
				dom.content(guard, this.renderGuard(cur));
			}, this));
		}, this), 1);

		return E([], [
			E('style', {}, css),
			E('h2', _('Phone (RJ11)')),
			E('div', { 'class': 'cbi-map-descr' }, _('Use a normal telephone on the RJ11 port to make and receive calls with the SIM.')),
			guard,
			E('div', { 'class': 'cbi-section' }, [
				E('div', { 'class': 'fxs-top' }, [
					this.phone,
					E('div', { 'style': 'flex: 1; min-width: 280px' }, [
						E('div', { 'style': 'margin-bottom: 12px' }, this.toggle),
						statusTable
					])
				])
			]),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', _('Line test')),
				lineTable,
				E('div', { 'style': 'margin-top: 10px' }, this.ringBtn)
			]),
			E('div', { 'style': 'text-align: right; opacity: 0.6; font-size: 90%' }, [
				E('a', { 'href': 'https://github.com/Perz3us/openwrt-aw1000-phone', 'target': '_blank', 'rel': 'noreferrer' },
					'openwrt-aw1000-phone by perz3us')
			])
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
