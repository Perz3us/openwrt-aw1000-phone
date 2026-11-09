'use strict';
'require view';
'require fxs.tabs';
'require rpc';
'require poll';
'require ui';
'require dom';

var callStatus = rpc.declare({ object: 'fxs', method: 'status' });
var callClearGuard = rpc.declare({ object: 'fxs', method: 'clear_guard' });
var callSetEnabled = rpc.declare({ object: 'fxs', method: 'set_enabled', params: [ 'enabled' ] });
var callRingTest = rpc.declare({ object: 'fxs', method: 'ring_test' });
var callVersion = rpc.declare({ object: 'fxs', method: 'version' });
var callBackup = rpc.declare({ object: 'fxs', method: 'backup' });
var callRestore = rpc.declare({ object: 'fxs', method: 'restore' });

var callQuiet = rpc.declare({ object: 'fxs', method: 'quiet_status' });
var callQuietSet = rpc.declare({ object: 'fxs', method: 'quiet_set', params: [ 'minutes' ] });
var callQuietSeen = rpc.declare({ object: 'fxs', method: 'quiet_seen' });

var RESTORE_FILE = '/tmp/fxs-restore.json';

function minutesUntil(hhmm) {
	var p = (hhmm || '').split(':'), now = new Date(), t = new Date(now);

	if (p.length != 2)
		return 0;
	t.setHours(+p[0], +p[1], 0, 0);
	if (t <= now)
		t.setDate(t.getDate() + 1);
	return Math.max(1, Math.round((t - now) / 60000));
}

function left(sec) {
	var h = Math.floor(sec / 3600), m = Math.ceil((sec % 3600) / 60);

	if (m == 60) {
		h++;
		m = 0;
	}
	return h ? _('%d h %d min').format(h, m) : _('%d min').format(Math.max(1, m));
}

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
.fxs-footer { margin-top: 24px; padding-top: 12px; border-top: 1px solid rgba(128,128,128,.25); text-align: center; font-size: 90%; opacity: .75; line-height: 1.7; }\
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
		[ _('Audio'), !st.running ? '-' : (st.hd ? _('HD (16 kHz)') : _('Normal (8 kHz)')) ]
	];

	if (st.running && st.sip)
		rows.push([ _('SIP calling'), st.sip < 2 ? _('Waiting for %s, the phone uses the modem directly').format(st.siptitle || _('the SIP service')) :
			(st.sipcall ? _('On, a SIP phone is on a call') : _('On')) ]);

	if (st.running && st.dnd)
		rows.push([ _('Do not disturb'), st.silent ? _('On, a call is coming in without ringing') :
			_('On, see Quiet below') ]);

	if (st.running && st.recmode && st.recmode != 'off')
		rows.push([ _('Call recording'), st.recactive ? _('Recording this call') : st.recfull ?
			_('On, but the space is full and calls are not being recorded. See the Recording tab.') :
			st.recusb ? _('On, saving to the USB stick') : _('On, saving to the router') ]);

	if (st.running && st.service === 0)
		rows.push([ _('Mobile service'), _('No mobile service, the phone cannot make calls. Emergency calls may still work.') ]);

	if (st.running)
		rows.push([ _('Phone on the line'), st.phone === 1 ? _('Yes') : st.phone === 0 ?
			_('Not found, check that a phone is plugged in') : _('Not checked yet. Press Ring the phone to check.') ]);

	rows = rows.concat([
		[ _('Number'), [ 'ringing', 'calling', 'talking' ].indexOf(st.state) >= 0 && st.peer ? st.peer : '-' ],
		[ _('Service running'), yesno(st.running) ],
		[ _('Kernel module loaded'), yesno(st.module) ],
		[ _('SLIC patch present'), yesno(st.patch) ]
	]);

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
		return Promise.all([ callStatus(), L.resolveDefault(callVersion(), {}), L.resolveDefault(callQuiet(), {}) ]);
	},

	updatePhone: function(st) {
		var s = phoneState(st);
		var off = st.running && (st.offhook || [ 'dialtone', 'digits', 'calling', 'talking', 'busy' ].indexOf(s) >= 0);

		this.phone.className = 'fxs-phone ' + s + (off ? ' offhook' : '') + (st.enabled === false ? ' disabled' : '');
		this.phoneLabel.textContent = st.enabled === false ? (st.running ? _('Phone port off, SIP only') : _('Phone port off')) : (stateText[s] || s);
		this.phonePeer.textContent = (s === 'ringing' || s === 'calling' || s === 'talking') && st.peer ? st.peer : '';
		this.phoneTimer.textContent = s === 'talking' && st.duration != null ? fmtDuration(st.duration) : '';

		this.toggle.textContent = st.enabled === false ? (st.sipon ? _('Phone port: OFF, SIP only') : _('Phone port: OFF')) : _('Phone port: ON');
		this.toggle.className = 'btn fxs-switch ' + (st.enabled === false ? 'cbi-button-negative' : 'cbi-button-positive');
		this.toggleNote.textContent = !st.sipon ? '' : st.enabled === false ?
			_('The RJ11 phone is off. Calls ring only on SIP phones and apps.') :
			_('Turn the port off to use only SIP phones and apps. SIP calls keep working.');

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
		if (st.running && st.phone === 0)
			return E('div', { 'class': 'alert-message warning' },
				E('p', _('No phone was found on the RJ11 port the last time it rang. Check that the phone is plugged in, then use Ring the phone to check again.')));
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

	handleBackup: function() {
		return callBackup().then(function(r) {
			var a = E('a', {
				'href': URL.createObjectURL(new Blob([ JSON.stringify(r) ], { type: 'application/json' })),
				'download': 'phone-backup-' + new Date().toISOString().slice(0, 10) + '.json'
			});

			document.body.appendChild(a);
			a.click();
			document.body.removeChild(a);
		});
	},

	handleRestore: function(ev) {
		var label = ev.target.firstChild, text = label.data;

		return ui.uploadFile(RESTORE_FILE, label).then(function() {
			ui.showModal(_('Restore phone settings'), [
				E('p', _('This replaces the phone settings, speed dial, blocked numbers and call history with the ones in the file, then restarts the phone service. It cannot be undone.')),
				E('div', { 'class': 'right' }, [
					E('button', { 'class': 'btn', 'click': function() {
						ui.hideModal();
					} }, _('Cancel')),
					' ',
					E('button', { 'class': 'btn cbi-button-negative', 'click': ui.createHandlerFn(this, function() {
						return callRestore().then(function(r) {
							ui.hideModal();
							if (r && r.result)
								window.location.reload();
							else
								ui.addNotification(null, E('p', r && r.error == 'busy' ?
									_('The phone is in use. Try again when it is idle.') :
									_('This is not a phone settings backup.')));
						});
					}) }, _('Restore'))
				])
			]);
		}).catch(function(e) {
			if (e.name != 'cancelled' && !/cancel/i.test(e.message || ''))
				ui.addNotification(null, E('p', _('Upload failed: %s').format(e.message)));
		}).finally(function() {
			label.data = text;
		});
	},

	quietLine: function(q) {
		if (q.on && q.until)
			return _('Quiet, %s left. The phone rings only for Important contacts and the Always ring list.').format(left(q.until - q.now));
		if (q.on)
			return _('Quiet until you end it. The phone rings only for Important contacts and the Always ring list.');
		if (q.scheduled)
			return _('Quiet now because of a Do not disturb window or holiday.');
		return _('Not quiet. The phone rings as normal.');
	},

	updateQuiet: function(q) {
		var on = !!(q && q.on);

		q = q || {};
		dom.content(this.quietText, this.quietLine(q));
		this.quietPick.style.display = on ? 'none' : '';
		this.quietStart.style.display = on ? 'none' : '';
		this.quietEnd.style.display = on ? '' : 'none';
		this.quietTime.style.display = !on && this.quietPick.value == 'until' ? '' : 'none';
		dom.content(this.quietCalls, q.quiet_calls > 0 && !on && !q.scheduled ? E('div', { 'class': 'alert-message' }, [
			q.quiet_calls == 1 ? _('1 call came in while it was quiet.') : _('%d calls came in while it was quiet.').format(q.quiet_calls),
			' ',
			E('a', { 'href': L.url('admin/services/fxs/history') }, _('See the call history')),
			' ',
			E('button', { 'class': 'btn', 'click': ui.createHandlerFn(this, function() {
				return callQuietSeen().then(L.bind(function() { return callQuiet().then(L.bind(this.updateQuiet, this)); }, this));
			}) }, _('Done'))
		]) : '');
	},

	handleQuiet: function(minutes) {
		if (minutes == null) {
			minutes = this.quietPick.value == 'until' ? minutesUntil(this.quietTime.value) : +this.quietPick.value;
		}
		return callQuietSet(minutes).then(function(r) {
			if (!r || !r.result)
				ui.addNotification(null, E('p', _('The phone service did not take the change. Is it running?')));
			return new Promise(function(res) { window.setTimeout(res, 600); });
		}).then(function() {
			return callQuiet();
		}).then(L.bind(this.updateQuiet, this));
	},

	quietSection: function() {
		this.quietText = E('p');
		this.quietPick = E('select', { 'class': 'cbi-input-select', 'style': 'width: auto', 'change': L.bind(function() {
			this.quietTime.style.display = this.quietPick.value == 'until' ? '' : 'none';
		}, this) }, [
			E('option', { 'value': '30' }, _('For 30 minutes')),
			E('option', { 'value': '60', 'selected': '' }, _('For 1 hour')),
			E('option', { 'value': '120' }, _('For 2 hours')),
			E('option', { 'value': 'until' }, _('Until a time')),
			E('option', { 'value': '-1' }, _('Until I end it'))
		]);
		this.quietTime = E('input', { 'type': 'time', 'value': '06:00', 'class': 'cbi-input-text', 'style': 'width: auto; display: none' });
		this.quietStart = E('button', { 'class': 'btn cbi-button-action', 'click': ui.createHandlerFn(this, 'handleQuiet', null) }, _('Go quiet'));
		this.quietEnd = E('button', { 'class': 'btn cbi-button-negative', 'click': ui.createHandlerFn(this, 'handleQuiet', 0) }, _('End now'));
		this.quietCalls = E('div');

		return E('div', { 'class': 'cbi-section' }, [
			E('h3', _('Quiet')),
			this.quietText,
			E('div', { 'style': 'display: flex; flex-wrap: wrap; gap: 8px; align-items: center' },
				[ this.quietPick, this.quietTime, this.quietStart, this.quietEnd ]),
			this.quietCalls
		]);
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
		var st = data[0] || {};
		var ver = data[1] || {};
		var verText = ver.fxsd ? _('Version %s, driver %s').format(ver.fxsd, ver.driver || '?') : '';

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
		this.toggleNote = E('div', { 'class': 'cbi-value-description', 'style': 'margin-top: 6px' });
		this.ringBtn = E('button', { 'click': ui.createHandlerFn(this, 'handleRingTest') });
		this.updatePhone(st);

		window.setTimeout(L.bind(this.updateQuiet, this, data[2] || {}), 0);
		poll.add(L.bind(function() {
			return L.resolveDefault(callQuiet(), {}).then(L.bind(this.updateQuiet, this));
		}, this), 10);
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
						E('div', { 'style': 'margin-bottom: 12px' }, [ this.toggle, this.toggleNote ]),
						statusTable
					])
				])
			]),
			this.quietSection(),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', _('Line test')),
				lineTable,
				E('div', { 'style': 'margin-top: 10px' }, this.ringBtn)
			]),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', _('Backup')),
				E('div', { 'class': 'cbi-value-description', 'style': 'margin-bottom: 10px' },
					_('Save or load the phone settings, speed dial, blocked numbers and call history.')),
				E('button', { 'class': 'btn cbi-button-action', 'click': ui.createHandlerFn(this, 'handleBackup') }, _('Save backup')),
				' ',
				E('button', { 'class': 'btn cbi-button-action', 'click': ui.createHandlerFn(this, 'handleRestore') }, _('Restore backup'))
			]),
			E('div', { 'class': 'fxs-footer' }, [
				E('div', {}, [
					E('a', { 'href': 'https://github.com/Perz3us/openwrt-aw1000-phone', 'target': '_blank', 'rel': 'noreferrer' },
						'openwrt-aw1000-phone'),
					' by perz3us',
					verText ? ' · ' + verText : ''
				]),
				E('div', {}, [
					_('Open source and open to contributions. Found a bug or have an idea? '),
					E('a', { 'href': 'https://github.com/Perz3us/openwrt-aw1000-phone/issues', 'target': '_blank', 'rel': 'noreferrer' },
						_('Open an issue')),
					_(' or send a pull request.')
				])
			])
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
