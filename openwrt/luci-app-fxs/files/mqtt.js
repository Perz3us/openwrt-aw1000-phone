'use strict';
'require view';
'require fxs.tabs';
'require form';
'require rpc';

var callMqtt = rpc.declare({ object: 'fxs', method: 'mqtt_status' });

return view.extend({
	load: function() {
		return callMqtt();
	},

	render: function(st) {
		var m, s, o, need = [];

		if (st && !st.client)
			need.push(_('Install mosquitto-client-nossl, for example: apk add mosquitto-client-nossl'));
		if (st && st.last_error)
			need.push(_('Last error: %s').format(st.last_error));

		m = new form.Map('fxs', _('MQTT'),
			_('Sends phone events like incoming, answered and missed to an MQTT broker, for Home Assistant and similar systems. Events and numbers go in plain text, so use a broker on your own network. Home Assistant also finds a Phone do not disturb switch and a Phone quiet minutes box, which work like the Quiet button on the Status page. Anyone who can write to the broker can change them.'));

		s = m.section(form.NamedSection, 'main', 'fxs');
		if (need.length)
			s.description = E('div', { 'class': 'alert-message warning' }, need.join(' '));

		o = s.option(form.Flag, 'mqtt', _('Send events'));
		o.default = '0';
		o.rmempty = false;

		o = s.option(form.Value, 'mqtt_host', _('Broker address'));
		o.datatype = 'host';
		o.depends('mqtt', '1');

		o = s.option(form.Value, 'mqtt_port', _('Port'));
		o.datatype = 'port';
		o.placeholder = '1883';
		o.depends('mqtt', '1');

		o = s.option(form.Value, 'mqtt_user', _('Username'));
		o.depends('mqtt', '1');

		o = s.option(form.Value, 'mqtt_password', _('Password'));
		o.password = true;
		o.depends('mqtt', '1');

		o = s.option(form.Value, 'mqtt_topic', _('Topic'),
			_('Events go to this topic followed by /event.'));
		o.placeholder = 'aw1000/phone';
		o.validate = function(section_id, value) {
			return !value || /^[A-Za-z0-9_\/-]+$/.test(value) ? true : _('Use letters, digits, _, - and /');
		};
		o.depends('mqtt', '1');

		o = s.option(form.Flag, 'mqtt_numbers', _('Include phone numbers'));
		o.default = '1';
		o.rmempty = false;
		o.depends('mqtt', '1');

		return m.render();
	}
});
