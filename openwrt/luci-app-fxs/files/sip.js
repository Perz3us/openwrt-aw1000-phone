'use strict';
'require view';
'require rpc';
'require poll';
'require ui';
'require dom';

var callServices = rpc.declare({ object: 'fxs', method: 'sip_services' });
var callSet = rpc.declare({ object: 'fxs', method: 'set_sip', params: [ 'enabled', 'name' ] });

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
		return callServices();
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
				ui.addNotification(null, E('p', _('SIP settings saved.')), 'info');
				return callServices().then(L.bind(this.refresh, this));
			}
			ui.addNotification(null, E('p', errorText[r && r.error] || _('Could not save the SIP settings.')));
		}, this));
	},

	render: function(r) {
		var services = r.services || [];

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
				}, _('Save')))
			])
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
