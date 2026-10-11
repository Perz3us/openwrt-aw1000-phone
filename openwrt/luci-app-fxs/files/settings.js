'use strict';
'require view';
'require form';

return view.extend({
	render: function() {
		var m, s, o;

		m = new form.Map('fxs', _('Phone (RJ11)'), _('Settings for the RJ11 phone port.'));

		s = m.section(form.NamedSection, 'main', 'fxs');
		s.tab('general', _('General'));
		s.tab('advanced', _('Advanced'));

		o = s.taboption('general', form.Value, 'own_number', _('My number'),
			_('Shown on the Status page. Most prepaid SIMs do not store their own number, so enter it here.'));
		o.placeholder = '0771234567';
		o.validate = function(section_id, value) {
			return !value || /^\+?[0-9]{3,20}$/.test(value) ? true : _('Enter a phone number');
		};

		o = s.taboption('general', form.ListValue, 'volume', _('Earpiece volume'),
			_('Modem output volume (AT+CLVL).'));
		[ '0', '1', '2', '3', '4', '5' ].forEach(function(v) { o.value(v); });

		o = s.taboption('general', form.Value, 'tx_gain', _('Microphone gain (dB)'),
			_('Level of your voice sent to the other side. Lower it if the other side hears distortion.'));
		o.datatype = 'range(-20,6)';

		o = s.taboption('general', form.Value, 'rx_gain', _('Earpiece gain (dB)'),
			_('Extra level of the other side in the handset.'));
		o.datatype = 'range(-20,6)';

		o = s.taboption('general', form.Flag, 'ringback', _('Ringback tone'),
			_('Play a ringing tone while the other phone rings. Turn it off if the network already sends one.'));
		o.default = '1';
		o.rmempty = false;

		o = s.taboption('general', form.ListValue, 'callerid', _('Caller ID'),
			_('Sends the caller\'s number to phones with a display after the first ring. If your phone shows nothing, try another format.'));
		o.value('bellcore', _('FSK (Bellcore)'));
		o.value('etsi', _('FSK (ETSI)'));
		o.value('dtmf', _('DTMF'));
		o.value('off', _('Off'));
		o.default = 'bellcore';

		o = s.taboption('advanced', form.Value, 'modem_rx_gain', _('Modem receive gain'),
			_('AT+QRXGAIN value, 8192 is unity.'));
		o.datatype = 'range(0,65535)';

		o = s.taboption('advanced', form.Value, 'led', _('Phone LED'),
			_('LED that is on while the port is on. Leave empty to not use an LED.'));
		o.placeholder = 'green:phone';

		o = s.taboption('advanced', form.ListValue, 'channel', _('SLIC channel'),
			_('Only channel 1 is wired on the AW1000.'));
		o.value('1');
		o.value('0');

		o = s.taboption('advanced', form.Value, 'at_port', _('Modem AT port'));

		o = s.taboption('advanced', form.Value, 'patch', _('SLIC patch file'));

		return m.render();
	}
});
