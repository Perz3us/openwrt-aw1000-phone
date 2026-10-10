'use strict';
'require view';
'require form';
'require rpc';
'require poll';
'require ui';
'require dom';

var callStatus = rpc.declare({ object: 'fxs', method: 'status' });
var callCalls = rpc.declare({ object: 'fxs', method: 'calls', expect: { calls: [] } });
var callClearGuard = rpc.declare({ object: 'fxs', method: 'clear_guard' });
var callSetEnabled = rpc.declare({ object: 'fxs', method: 'set_enabled', params: [ 'enabled' ] });

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
	stopped: _('Stopped')
};

var eventText = {
	incoming: _('Incoming'),
	answered: _('Answered'),
	missed: _('Missed'),
	dial: _('Dialed'),
	connected: _('Connected'),
	failed: _('Failed'),
	ended: _('Ended by remote'),
	hangup: _('Hung up')
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
.fxs-phone.disabled .fxs-state::before, .fxs-phone.stopped .fxs-state::before, .fxs-phone.waiting_modem .fxs-state::before { background: #8b919b; animation: none; }\
.fxs-phone.modem_master .fxs-state::before { background: #e5534b; }\
.fxs-phone.disabled svg, .fxs-phone.waiting_modem svg, .fxs-phone.modem_master svg { opacity: .3; filter: grayscale(1); }\
.fxs-phone .fxs-peer { margin-top: 6px; opacity: .75; min-height: 1.2em; font-variant-numeric: tabular-nums; }\
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

function statusRows(st) {
	var rows = [
		[ _('Line'), stateText[st.state] || st.state ],
		[ _('Handset'), st.offhook ? _('off hook') : _('on hook') ],
		[ _('Number'), st.peer || '-' ],
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

function callRows(calls) {
	if (!calls.length)
		return [ E('tr', { 'class': 'tr placeholder' }, [
			E('td', { 'class': 'td' }, E('em', _('No calls yet')))
		]) ];

	return calls.slice().reverse().map(function(c) {
		return E('tr', { 'class': 'tr' }, [
			E('td', { 'class': 'td left' }, new Date(c.time * 1000).toLocaleString()),
			E('td', { 'class': 'td left' }, eventText[c.event] || c.event),
			E('td', { 'class': 'td left' }, c.number || _('unknown'))
		]);
	});
}

function phoneState(st) {
	if (!st.running)
		return 'stopped';
	return st.state || 'idle';
}

return view.extend({
	load: function() {
		return Promise.all([ callStatus(), callCalls() ]);
	},

	updatePhone: function(st) {
		var s = phoneState(st);
		var off = st.running && (st.offhook || [ 'dialtone', 'digits', 'calling', 'talking', 'busy' ].indexOf(s) >= 0);

		this.phone.className = 'fxs-phone ' + s + (off ? ' offhook' : '') + (st.enabled === false ? ' disabled' : '');
		this.phoneLabel.textContent = st.enabled === false ? _('Phone port off') : (stateText[s] || s);
		this.phonePeer.textContent = (s === 'ringing' || s === 'calling' || s === 'talking') && st.peer ? st.peer : '';

		this.toggle.textContent = st.enabled === false ? _('Phone port: OFF') : _('Phone port: ON');
		this.toggle.className = 'btn fxs-switch ' + (st.enabled === false ? 'cbi-button-negative' : 'cbi-button-positive');
		this.enabled = st.enabled !== false;
	},

	renderGuard: function(st) {
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
		var st = data[0] || {}, calls = data[1] || [];
		var m, s, o;

		var statusTable = E('table', { 'class': 'table' }, statusRows(st));
		var callTable = E('table', { 'class': 'table' }, callRows(calls));
		var guard = E('div', {}, this.renderGuard(st));

		this.phone = E('div', { 'class': 'fxs-phone' });
		this.phone.innerHTML = phoneSvg;
		this.phoneLabel = E('div', { 'class': 'fxs-state' });
		this.phonePeer = E('div', { 'class': 'fxs-peer' });
		this.phone.appendChild(this.phoneLabel);
		this.phone.appendChild(this.phonePeer);
		this.toggle = E('button', { 'click': ui.createHandlerFn(this, 'handleToggle') });
		this.updatePhone(st);

		poll.add(L.bind(function() {
			return Promise.all([ callStatus(), callCalls() ]).then(L.bind(function(r) {
				var cur = r[0] || {};

				this.updatePhone(cur);
				dom.content(statusTable, statusRows(cur));
				dom.content(callTable, callRows(r[1] || []));
				dom.content(guard, this.renderGuard(cur));
			}, this));
		}, this), 1);

		m = new form.Map('fxs', _('Phone (RJ11)'),
			_('Use a normal telephone on the RJ11 port to make and receive calls with the SIM.'));

		s = m.section(form.NamedSection, 'main', 'fxs', _('Settings'));

		o = s.option(form.ListValue, 'volume', _('Earpiece volume'),
			_('Modem output volume (AT+CLVL).'));
		[ '0', '1', '2', '3', '4', '5' ].forEach(function(v) { o.value(v); });

		o = s.option(form.Value, 'tx_gain', _('Microphone gain (dB)'),
			_('Level of your voice sent to the other side. Lower it if the other side hears distortion.'));
		o.datatype = 'range(-20,6)';

		o = s.option(form.Value, 'rx_gain', _('Earpiece gain (dB)'),
			_('Extra level of the other side in the handset.'));
		o.datatype = 'range(-20,6)';

		o = s.option(form.Value, 'modem_rx_gain', _('Modem receive gain'),
			_('AT+QRXGAIN value, 8192 is unity.'));
		o.datatype = 'range(0,65535)';

		o = s.option(form.ListValue, 'channel', _('SLIC channel'),
			_('Only channel 1 is wired on the AW1000.'));
		o.value('1');
		o.value('0');

		o = s.option(form.Value, 'at_port', _('Modem AT port'));

		o = s.option(form.Value, 'patch', _('SLIC patch file'));

		return m.render().then(L.bind(function(node) {
			return E([], [
				E('style', {}, css),
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
				E('div', { 'class': 'cbi-section' }, [ E('h3', _('Recent calls')), callTable ]),
				node
			]);
		}, this));
	}
});
