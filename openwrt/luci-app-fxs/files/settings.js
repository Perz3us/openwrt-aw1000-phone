'use strict';
'require view';
'require form';

return view.extend({
	render: function() {
		var m, s, o;

		m = new form.Map('fxs', _('Phone (RJ11)'), _('Settings for the RJ11 phone port.'));

		s = m.section(form.NamedSection, 'main', 'fxs');
		s.tab('general', _('General'));
		s.tab('dnd', _('Do not disturb'));
		s.tab('history', _('Call history'));
		s.tab('led', _('LED'));
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

		o = s.taboption('general', form.Flag, 'hd', _('HD calling'),
			_('Wideband (16 kHz) audio to the phone when the network and the other side support it. Changing this restarts the modem, so mobile data drops for about 30 seconds.'));
		o.default = '0';
		o.rmempty = false;

		o = s.taboption('general', form.Flag, 'sip', _('SIP calling (experimental)'),
			_('Lets SIP phones and apps make and answer calls through the SIM, next to the RJ11 phone. It needs QModem voice (qmodem-voip and qmodem-sipd, with at-daemon 3.4.0 or newer) set up and turned on in QModem. Until QModem voice is running, the RJ11 phone keeps working as before.') + '<br /><strong>' +
			_('Warning: this is still being tested. Calls may drop or have no sound, and call waiting (flash) on the RJ11 phone does not work while it is on. If something goes wrong, turn it off and report it on') +
			' <a href="https://github.com/Perz3us/openwrt-aw1000-phone/issues" target="_blank" rel="noreferrer">GitHub</a>.</strong>');
		o.default = '0';
		o.rmempty = false;

		o = s.taboption('general', form.ListValue, 'callerid', _('Caller ID'),
			_('Sends the caller\'s number to phones with a display. If your phone shows nothing, try another format or timing.'));
		o.value('bellcore', _('FSK (Bellcore)'));
		o.value('etsi', _('FSK (ETSI)'));
		o.value('dtmf', _('DTMF'));
		o.value('off', _('Off'));
		o.default = 'bellcore';

		o = s.taboption('general', form.ListValue, 'callerid_timing', _('Caller ID timing'),
			_('Most phones in North America expect the number after the first ring. Most phones in Europe expect it before the first ring, usually with FSK (ETSI) or DTMF.'));
		o.value('after', _('After the first ring'));
		o.value('before', _('Before the first ring'));
		o.value('rpas', _('Short ring, then caller ID (RP-AS, FSK only)'));
		o.default = 'after';
		o.depends({ callerid: 'off', '!reverse': true });

		o = s.taboption('general', form.ListValue, 'callerid_format', _('Caller ID number'),
			_('Caller ID can only send digits, so the + of a number is lost. Local shows numbers from your own country the way you dial them, and adds the international prefix (like 00) to other numbers. The call history always keeps the full number.'));
		o.value('received', _('As received, without the +'));
		o.value('local', _('Local'));
		o.default = 'received';
		o.depends({ callerid: 'off', '!reverse': true });

		o = s.taboption('general', form.Value, 'callerid_country', _('Country code'),
			_('Found from the SIM when set to auto. Enter it without the +, for example 94 or 351.'));
		o.value('auto');
		o.placeholder = 'auto';
		o.default = 'auto';
		o.depends('callerid_format', 'local');
		o.validate = function(section_id, value) {
			return !value || value == 'auto' || /^[0-9]{1,4}$/.test(value) ? true : _('Enter a country code like 94');
		};

		o = s.taboption('general', form.Value, 'callerid_prefix', _('Local prefix'),
			_('The digit you dial before a local number instead of the country code, 0 in most countries. Use none if your country has no prefix, like Portugal or Italy.'));
		o.value('auto');
		o.value('none', _('none'));
		o.value('0');
		o.placeholder = 'auto';
		o.default = 'auto';
		o.depends('callerid_format', 'local');
		o.validate = function(section_id, value) {
			return !value || value == 'auto' || value == 'none' || /^[0-9]{1,3}$/.test(value) ? true : _('Enter digits, auto or none');
		};

		o = s.taboption('general', form.ListValue, 'ring_pattern', _('Ring pattern'),
			_('How the phone rings for an incoming call. Use Ring the phone on the Status page to try it.'));
		o.value('standard', _('Standard (2 s ring, 4 s pause)'));
		o.value('short', _('Short (1 s ring, 4 s pause)'));
		o.value('double', _('Double ring (UK and Australia)'));
		o.value('custom', _('Custom'));
		o.default = 'standard';

		o = s.taboption('general', form.Value, 'ring_on', _('Ring time (ms)'));
		o.datatype = 'range(200,8000)';
		o.placeholder = '2000';
		o.depends('ring_pattern', 'custom');

		o = s.taboption('general', form.Value, 'ring_off', _('Pause time (ms)'));
		o.datatype = 'range(200,8000)';
		o.placeholder = '4000';
		o.depends('ring_pattern', 'custom');

		o = s.taboption('dnd', form.Flag, 'dnd', _('Do not disturb'),
			_('The RJ11 phone does not ring during these hours. Calls still show in the call history as missed, and SIP phones still ring. If the same number calls again within 3 minutes, the phone rings, so urgent calls get through.'));
		o.default = '0';
		o.rmempty = false;

		[ [ 'dnd_from', _('From'), '22:00' ], [ 'dnd_to', _('Until'), '07:00' ] ].forEach(function(t) {
			o = s.taboption('dnd', form.Value, t[0], t[1]);
			o.placeholder = t[2];
			o.default = t[2];
			o.depends('dnd', '1');
			o.validate = function(section_id, value) {
				return !value || /^([01]?[0-9]|2[0-3]):[0-5][0-9]$/.test(value) ? true : _('Enter a time like 22:00');
			};
		});

		o = s.taboption('history', form.Flag, 'history_autodelete', _('Delete old calls'),
			_('Remove calls from the call history once they are older than the number of days below.'));
		o.default = '0';
		o.rmempty = false;

		o = s.taboption('history', form.Value, 'history_days', _('Keep calls for (days)'));
		o.datatype = 'range(1,3650)';
		o.placeholder = '30';
		o.default = '30';
		o.depends('history_autodelete', '1');

		o = s.taboption('advanced', form.Value, 'modem_rx_gain', _('Modem receive gain'),
			_('AT+QRXGAIN value, 8192 is unity.'));
		o.datatype = 'range(0,65535)';

		[
			[ 'led_ready', _('Line ready'), 'on' ],
			[ 'led_ringing', _('Incoming call'), 'fast' ],
			[ 'led_offhook', _('Handset off hook'), 'slow' ]
		].forEach(function(l) {
			o = s.taboption('led', form.ListValue, l[0], l[1]);
			o.value('on', _('Solid'));
			o.value('slow', _('Slow blink'));
			o.value('fast', _('Fast blink'));
			o.value('off', _('Off'));
			o.default = l[2];
		});

		o = s.taboption('led', form.Value, 'led', _('LED'),
			_('The AW1000 phone LED is green only. Leave empty to not use an LED. When the port is off the LED is off. During NoobWRT\'s LED night mode the phone LED stays dark and only blinks while a call rings.'));
		o.placeholder = 'green:phone';

		o = s.taboption('advanced', form.ListValue, 'channel', _('SLIC channel'),
			_('Only channel 1 is wired on the AW1000.'));
		o.value('1');
		o.value('0');

		o = s.taboption('advanced', form.Value, 'at_port', _('Modem AT port'),
			_('auto finds the modem AT port by itself, also when the port names change after a modem restart.'));
		o.value('auto');
		o.placeholder = 'auto';

		o = s.taboption('advanced', form.Value, 'patch', _('SLIC patch file'));

		return m.render();
	}
});
