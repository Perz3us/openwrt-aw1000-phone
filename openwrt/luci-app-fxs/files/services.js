'use strict';
'require view';
'require fxs.tabs';
'require rpc';
'require ui';
'require dom';

var callForwardGet = rpc.declare({ object: 'fxs', method: 'forward_get', params: [ 'reason' ] });
var callForwardSet = rpc.declare({ object: 'fxs', method: 'forward_set', params: [ 'reason', 'number', 'seconds' ] });
var callUssd = rpc.declare({ object: 'fxs', method: 'ussd', params: [ 'code', 'reply' ] });
var callUssdEnd = rpc.declare({ object: 'fxs', method: 'ussd_end' });

var reasons = [
	[ 'always', _('All calls') ],
	[ 'busy', _('When busy') ],
	[ 'noanswer', _('When not answered') ],
	[ 'unreachable', _('When not reachable') ]
];

function errorText(e) {
	return {
		refused: _('Not available. This modem and SIM could not do this on the current mobile network.'),
		noanswer: _('No answer from the mobile network. Check the code, or try again later.'),
		busy: _('The phone is in use. Try again when it is idle.'),
		timeout: _('The modem did not answer in time. Try again.'),
		stopped: _('The phone service is not running.'),
		number: _('Enter a phone number.'),
		seconds: _('Enter 5 to 30 seconds.'),
		code: _('Enter a code like *123#.')
	}[e] || _('Something went wrong (%s).').format(e || '?');
}

return view.extend({
	stateText: function(r) {
		var p;

		if (!r || !r.ok)
			return errorText(r && r.error);
		if (r.text == 'off')
			return _('Off');
		p = (r.text || '').split(',');
		return +p[2] > 0 ? _('On, to %s after %d seconds').format(p[1], +p[2]) : _('On, to %s').format(p[1]);
	},

	readAll: function() {
		return reasons.reduce(L.bind(function(chain, t) {
			return chain.then(L.bind(function() {
				return callForwardGet(t[0]).then(L.bind(function(r) {
					dom.content(this.state[t[0]], this.stateText(r));
				}, this));
			}, this));
		}, this), Promise.resolve());
	},

	setForward: function(reason, clear) {
		var number = clear ? '' : this.number[reason].value.trim(),
		    seconds = clear || !this.seconds[reason] ? '' : this.seconds[reason].value.trim();

		return callForwardSet(reason, number, seconds).then(L.bind(function(r) {
			if (!r || !r.ok) {
				ui.addNotification(null, E('p', errorText(r && r.error)));
				return;
			}
			return callForwardGet(reason).then(L.bind(function(s) {
				dom.content(this.state[reason], this.stateText(s));
			}, this));
		}, this));
	},

	forwardRow: function(t) {
		this.state[t[0]] = E('span', _('Checking...'));
		this.number[t[0]] = E('input', { 'class': 'cbi-input-text', 'type': 'text', 'placeholder': '0771234567' });
		if (t[0] == 'noanswer')
			this.seconds[t[0]] = E('input', { 'class': 'cbi-input-text', 'type': 'number', 'min': 5, 'max': 30, 'step': 5, 'value': 20, 'style': 'width: 5em' });

		return E('tr', { 'class': 'tr' }, [
			E('td', { 'class': 'td left' }, t[1]),
			E('td', { 'class': 'td left' }, this.state[t[0]]),
			E('td', { 'class': 'td left' }, [ this.number[t[0]] ].concat(this.seconds[t[0]] ? [ ' ', this.seconds[t[0]], ' ', _('seconds') ] : [])),
			E('td', { 'class': 'td left' }, [
				E('button', { 'class': 'btn cbi-button-save', 'click': ui.createHandlerFn(this, 'setForward', t[0], false) }, _('Set')),
				' ',
				E('button', { 'class': 'btn cbi-button-reset', 'click': ui.createHandlerFn(this, 'setForward', t[0], true) }, _('Clear'))
			])
		]);
	},

	sendUssd: function(isReply) {
		var value = (isReply ? this.ussdReply : this.ussdCode).value.trim();

		dom.content(this.ussdOut, E('em', _('Waiting for the network...')));
		return callUssd(isReply ? '' : value, isReply ? value : '').then(L.bind(function(r) {
			this.ussdReply.value = '';
			this.ussdMore.style.display = r && r.ok && r.more ? '' : 'none';
			if (!r || !r.ok) {
				dom.content(this.ussdOut, errorText(r && r.error));
				return;
			}
			dom.content(this.ussdOut, E('pre', { 'style': 'white-space: pre-wrap' }, (r.text || _('Done.')).split('|').join('\n')));
		}, this));
	},

	endUssd: function() {
		this.ussdMore.style.display = 'none';
		dom.content(this.ussdOut, '');
		return callUssdEnd();
	},

	render: function() {
		this.state = {};
		this.number = {};
		this.seconds = {};
		this.ussdCode = E('input', { 'class': 'cbi-input-text', 'type': 'text', 'placeholder': '*123#' });
		this.ussdReply = E('input', { 'class': 'cbi-input-text', 'type': 'text', 'style': 'width: 6em' });
		this.ussdOut = E('div', { 'style': 'margin-top: 8px' });
		this.ussdMore = E('p', { 'style': 'display: none' }, [
			this.ussdReply, ' ',
			E('button', { 'class': 'btn cbi-button-action', 'click': ui.createHandlerFn(this, 'sendUssd', true) }, _('Reply')), ' ',
			E('button', { 'class': 'btn cbi-button-reset', 'click': ui.createHandlerFn(this, 'endUssd') }, _('End'))
		]);

		window.setTimeout(L.bind(this.readAll, this), 0);

		return E([], [
			E('h2', _('Phone services')),
			E('h3', _('Call forwarding')),
			E('div', { 'class': 'cbi-map-descr' }, _('The network keeps this setting on your SIM, so it also applies to calls for SIP phones. Your network may charge for forwarded calls. This needs support from your modem, SIM and mobile network, so it does not work on every connection.')),
			E('table', { 'class': 'table' }, [
				E('tr', { 'class': 'tr table-titles' }, [
					E('th', { 'class': 'th left' }, _('Forward')),
					E('th', { 'class': 'th left' }, _('Now')),
					E('th', { 'class': 'th left' }, _('Number')),
					E('th', { 'class': 'th left' })
				])
			].concat(reasons.map(L.bind(this.forwardRow, this)))),
			E('h3', _('USSD')),
			E('div', { 'class': 'cbi-map-descr' }, _('Send a code like a balance check and read the answer here. The codes depend on your network, and this does not work on every modem and SIM.')),
			E('p', {}, [
				this.ussdCode, ' ',
				E('button', { 'class': 'btn cbi-button-action', 'click': ui.createHandlerFn(this, 'sendUssd', false) }, _('Send'))
			]),
			this.ussdOut,
			this.ussdMore
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
