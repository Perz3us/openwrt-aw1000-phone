'use strict';
'require view';
'require fxs.tabs';
'require rpc';
'require poll';
'require ui';
'require dom';
'require uqr';

var callServices = rpc.declare({ object: 'fxs', method: 'sip_services' });
var callSet = rpc.declare({ object: 'fxs', method: 'set_sip', params: [ 'enabled', 'name' ] });
var callVpn = rpc.declare({ object: 'fxs', method: 'sip_vpn_status' });
var callSetVpn = rpc.declare({ object: 'fxs', method: 'set_sip_vpn', params: [ 'enabled', 'interface' ] });
var callQr = rpc.declare({ object: 'fxs', method: 'sip_qr' });

var statusText = {
	running: _('Running'),
	stopped: _('Stopped'),
	'not-installed': _('Not installed')
};

var errorText = {
	busy: _('The phone is in use. Try again when it is idle.'),
	name: _('Choose a SIP service first.'),
	'not-installed': _('This SIP service is not installed.')
};

function plain(s) {
	return document.createTextNode(s == null ? '' : s);
}

function titleOf(r, name) {
	var s = (r.services || []).filter(function(x) { return x.name == name; })[0];

	return s ? (s.title || s.name) : name;
}

function lineText(r) {
	if (!r.enabled)
		return _('Off. SIP phones and apps cannot use the line.');
	if (r.connected)
		return _('On, connected to %s.').format(titleOf(r, r.current));
	return _('On, waiting for %s. The RJ11 phone uses the modem directly until then.').format(titleOf(r, r.current));
}

function neededList(items) {
	if (!items.length)
		return '';
	return E('div', { 'style': 'margin-top: 6px' }, [
		E('strong', _('Still needed:')),
		E('ul', { 'style': 'margin: 4px 0 0 18px' }, items.map(function(t) { return E('li', {}, plain(t)); }))
	]);
}

function setupText(s) {
	var items = s.setup || [];

	if (!items.length)
		return '';
	return E('div', { 'style': 'margin-top: 6px' }, [
		E('strong', _('Still needed:')),
		E('ul', { 'style': 'margin: 4px 0 0 18px' }, items.map(function(t) { return E('li', {}, plain(t)); }))
	]);
}

return view.extend({
	load: function() {
		return Promise.all([ callServices(), L.resolveDefault(callVpn(), {}) ]);
	},

	vpnText: function(v) {
		if (v.active)
			return _('On. SIP phones can connect through %s.').format(v.interface);
		if (v.enabled)
			return _('On, but not working yet. See below what is still needed.');
		return _('Off. SIP phones can only connect from this router\'s own network.');
	},

	vpnNeeds: function(v) {
		var items = (v.needs || []).slice();

		if (!(v.vpns || []).length)
			items.unshift(_('No VPN found. Set up WireGuard or Tailscale on the router first.'));
		if (v.down)
			items.push(_('The VPN is down.'));
		return items;
	},

	refreshVpn: function(v) {
		this.vpn = v || {};
		dom.content(this.vpnLine, plain(this.vpnText(this.vpn)));
		dom.content(this.vpnNeedBox, neededList(this.vpn.enabled ? this.vpnNeeds(this.vpn) : (this.vpn.vpns || []).length ? [] : this.vpnNeeds(this.vpn)));
	},

	say: function(box, text, bad) {
		window.clearTimeout(box.timer);
		dom.content(box, E('div', { 'class': bad ? 'alert-message warning' : 'alert-message notice' }, text));
		if (!bad)
			box.timer = window.setTimeout(function() { dom.content(box, ''); }, 4000);
	},

	saveVpn: function() {
		return callSetVpn(this.vpnToggle.checked, this.vpnSelect.value).then(L.bind(function(r) {
			if (!r || !r.result) {
				this.say(this.vpnMsg, _('Choose the VPN connection first.'), true);
				return;
			}
			this.say(this.vpnMsg, _('VPN setting saved.'));
			return callVpn().then(L.bind(this.refreshVpn, this));
		}, this));
	},

	vpnSection: function(v) {
		var vpns = v.vpns || [];

		this.vpnToggle = E('input', { 'type': 'checkbox', 'id': 'sip_vpn' });
		this.vpnToggle.checked = !!v.enabled;
		this.vpnSelect = E('select', { 'class': 'cbi-input-select' }, vpns.length ? vpns.map(function(d) {
			return E('option', { 'value': d, 'selected': d == v.interface ? '' : null }, d);
		}) : [ E('option', { 'value': '' }, _('No VPN found')) ]);
		this.vpnLine = E('p', {}, plain(this.vpnText(v)));
		this.vpnNeedBox = E('div');

		return E('div', { 'class': 'cbi-section' }, [
			E('h3', _('Use over a VPN')),
			E('div', { 'class': 'cbi-map-descr' }, _('Lets a SIP phone or app use the line from outside your home, through a VPN that ends on this router.')),
			E('div', { 'class': 'alert-message warning' }, _('Never open SIP to the internet. Anyone who gets in can make calls on your SIM.')),
			this.vpnLine,
			E('p', {}, [
				this.vpnToggle, ' ',
				E('label', { 'for': 'sip_vpn' }, _('Allow SIP over the VPN')), ' ',
				this.vpnSelect
			]),
			this.vpnNeedBox,
			E('ul', { 'style': 'margin: 8px 0 0 18px' }, [
				E('li', {}, plain(_('On the phone, the VPN has to send %s through the tunnel. With Tailscale, approve this router\'s subnet route in the Tailscale admin page.').format(v.lan || _('the router\'s address')))),
				E('li', {}, plain(_('Using a VPN app while on this router\'s own Wi-Fi? In the app, allow LAN access, or leave the SIP app out of the VPN.')))
			]),
			E('div', { 'style': 'margin-top: 10px' }, E('button', {
				'class': 'btn cbi-button-save',
				'click': ui.createHandlerFn(this, 'saveVpn')
			}, _('Save'))),
			this.vpnMsg = E('div', { 'style': 'margin-top: 8px' })
		]);
	},

	qrImage: function(url) {
		var svg = new DOMParser().parseFromString(uqr.renderSVG(url), 'image/svg+xml').documentElement,
		    size = svg.getAttribute('width');

		svg.setAttribute('viewBox', '0 0 ' + size + ' ' + size);
		svg.setAttribute('shape-rendering', 'crispEdges');
		svg.setAttribute('width', '100%');
		svg.setAttribute('height', '100%');
		return svg;
	},

	closeQr: function() {
		window.clearInterval(this.qrTimer);
		ui.hideModal();
	},

	showQr: function() {
		return callQr().then(L.bind(function(r) {
			var end, frame, clock, again, tick;

			window.clearInterval(this.qrTimer);
			if (!r || !r.url) {
				dom.content(this.qrBox, neededList((r && r.needs && r.needs.length) ? r.needs : [ _('Could not make the code.') ]));
				return;
			}
			dom.content(this.qrBox, '');
			end = Date.now() + (r.expires || 300) * 1000;
			frame = E('div', { 'style': 'box-sizing: border-box; background: #fff; padding: 12px; width: min(280px, 70vw) !important; height: min(280px, 70vw); max-width: 280px; flex: 0 0 auto; border-radius: 10px; line-height: 0; transition: opacity .3s' }, this.qrImage(r.url));
			clock = E('div', { 'style': 'text-align: center; font-size: 1.15em; margin: 12px 0 4px' });
			again = E('button', { 'class': 'btn cbi-button-action', 'style': 'display: none', 'click': ui.createHandlerFn(this, function() {
				this.closeQr();
				return this.showQr();
			}) }, _('New code'));
			tick = L.bind(function() {
				var s = Math.max(0, Math.round((end - Date.now()) / 1000));

				if (s > 0) {
					dom.content(clock, _('Works for %d:%02d').format(Math.floor(s / 60), s % 60));
					return;
				}
				window.clearInterval(this.qrTimer);
				frame.style.opacity = '.15';
				dom.content(clock, _('This code has run out.'));
				again.style.display = '';
			}, this);
			tick();
			this.qrTimer = window.setInterval(tick, 1000);

			ui.showModal(_('Scan with Linphone'), [
				E('p', { 'style': 'text-align: center' }, plain(_('In Linphone, open the assistant and choose to scan a QR code or fetch a remote configuration. The phone must be on this router\'s Wi-Fi.'))),
				E('div', { 'style': 'display: flex; justify-content: center' }, frame),
				clock,
				E('p', { 'style': 'text-align: center; opacity: .8' }, plain(_('By hand instead: server %s, account %s, and the password from your SIP service.').format(r.server, r.user))),
				r.note ? E('div', { 'class': 'alert-message warning' }, plain(r.note)) : '',
				E('div', { 'class': 'right' }, [
					again,
					' ',
					E('button', { 'class': 'btn', 'click': L.bind(this.closeQr, this) }, _('Close'))
				])
			]);
		}, this));
	},

	qrSection: function() {
		this.qrBox = E('div', { 'style': 'margin-top: 10px' });

		return E('div', { 'class': 'cbi-section' }, [
			E('h3', _('Set up a phone app')),
			E('div', { 'class': 'cbi-map-descr' }, _('Scan a code with Linphone to add the SIP account to a phone. In Linphone, choose to fetch a remote configuration or scan a QR code. The phone must be on this router\'s network.')),
			E('div', { 'class': 'alert-message warning' }, _('Anyone who scans this code or opens its link in the next 5 minutes gets the SIP password.')),
			E('button', { 'class': 'btn cbi-button-action', 'click': ui.createHandlerFn(this, 'showQr') }, _('Show QR code')),
			this.qrBox
		]);
	},

	selected: function() {
		var c = this.list.querySelector('input[name="sip_service"]:checked');

		return c ? c.value : this.current;
	},

	serviceRow: function(s, current) {
		var off = s.status == 'not-installed',
		    radio = E('input', { 'type': 'radio', 'name': 'sip_service', 'value': s.name });

		radio.checked = s.name == current;
		radio.disabled = off;
		this.setupBox[s.name] = E('div', {}, setupText(s));

		return E('tr', { 'class': 'tr' }, [
			E('td', { 'class': 'td', 'width': '5%' }, radio),
			E('td', { 'class': 'td left' }, [ plain(s.title || s.name), this.setupBox[s.name] ]),
			E('td', { 'class': 'td left' }, plain(statusText[s.status] || s.status)),
			E('td', { 'class': 'td left' }, s.audio == 16000 ? _('HD (16 kHz)') : _('8 kHz')),
			E('td', { 'class': 'td left' }, off && s.url ? E('a', { 'href': s.url, 'target': '_blank', 'rel': 'noreferrer' }, _('Get it')) : '')
		]);
	},

	refresh: function(r) {
		this.current = r.current;
		dom.content(this.line, plain(lineText(r)));
		(r.services || []).forEach(L.bind(function(s) {
			if (this.setupBox[s.name])
				dom.content(this.setupBox[s.name], setupText(s));
		}, this));
	},

	saveSip: function() {
		return callSet(this.toggle.checked, this.selected()).then(L.bind(function(r) {
			if (r && r.result) {
				this.say(this.sipMsg, _('SIP settings saved.'));
				return callServices().then(L.bind(this.refresh, this));
			}
			this.say(this.sipMsg, errorText[r && r.error] || _('Could not save the SIP settings.'), true);
		}, this));
	},

	render: function(data) {
		var r = data[0], services = r.services || [];

		this.current = r.current;
		this.setupBox = {};
		this.toggle = E('input', { 'type': 'checkbox', 'id': 'sip_enabled' });
		this.toggle.checked = !!r.enabled;
		this.line = E('p', {}, plain(lineText(r)));
		this.list = E('table', { 'class': 'table' }, [
			E('tr', { 'class': 'tr table-titles' }, [
				E('th', { 'class': 'th' }),
				E('th', { 'class': 'th left' }, _('Service')),
				E('th', { 'class': 'th left' }, _('Status')),
				E('th', { 'class': 'th left' }, _('Audio')),
				E('th', { 'class': 'th left' })
			])
		].concat(services.length ? services.map(L.bind(function(s) {
			return this.serviceRow(s, r.current);
		}, this)) : [
			E('tr', { 'class': 'tr placeholder' }, E('td', { 'class': 'td', 'colspan': 5 }, E('em', _('No SIP services are installed.'))))
		]));

		poll.add(L.bind(function() {
			return callServices().then(L.bind(this.refresh, this));
		}, this), 5);
		window.setTimeout(L.bind(this.refreshVpn, this, data[1] || {}), 0);

		return E([], [
			E('h2', _('SIP')),
			E('div', { 'class': 'cbi-map-descr' }, _('Lets SIP phones and apps make and answer calls through the SIM, next to the RJ11 phone. The SIP side is done by the service you choose here. To use only SIP phones, turn the phone port off on the Status page.')),
			E('div', { 'class': 'alert-message warning' }, [
				_('This is still being tested. Calls may drop or have no sound. While SIP is on, FLASH on the RJ11 phone does not work and call waiting is turned off. If something goes wrong, turn it off and report it on '),
				E('a', { 'href': 'https://github.com/Perz3us/openwrt-aw1000-phone/issues', 'target': '_blank', 'rel': 'noreferrer' }, 'GitHub'),
				'.'
			]),
			E('div', { 'class': 'cbi-section' }, [
				this.line,
				E('p', {}, [
					this.toggle,
					' ',
					E('label', { 'for': 'sip_enabled' }, _('Turn on SIP'))
				]),
				E('h3', _('Service')),
				this.list,
				E('div', { 'style': 'margin-top: 10px' }, E('button', {
					'class': 'btn cbi-button-save',
					'click': ui.createHandlerFn(this, 'saveSip')
				}, _('Save'))),
				this.sipMsg = E('div', { 'style': 'margin-top: 8px' })
			]),
			this.vpnSection(data[1] || {}),
			this.qrSection()
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
