'use strict';
'require view';
'require fxs.tabs';
'require form';
'require rpc';

var callStatus = rpc.declare({ object: 'fxs', method: 'status' });

return view.extend({
	load: function() {
		return callStatus();
	},

	render: function(st) {
		var m, s, o, warn = [];

		if (st && st.clock_ok === false)
			warn.push(_('The router\'s clock is not set, so alarms will not ring.'));
		if (st && st.enabled === false)
			warn.push(_('The phone port is off, so alarms will not ring.'));

		m = new form.Map('fxs', _('Alarm calls'),
			_('The RJ11 phone rings at these times, also during do not disturb. Lifting the handset stops it. If the phone is in use at that time, the alarm is skipped.'));

		s = m.section(form.GridSection, 'alarm');
		s.anonymous = true;
		s.addremove = true;
		s.nodescriptions = true;
		if (warn.length)
			s.description = E('div', { 'class': 'alert-message warning' }, warn.join(' '));

		o = s.option(form.Flag, 'enabled', _('On'));
		o.default = '1';
		o.rmempty = false;
		o.editable = true;

		o = s.option(form.Value, 'name', _('Name'));

		o = s.option(form.Value, 'time', _('Time'));
		o.placeholder = '07:00';
		o.rmempty = false;
		o.validate = function(section_id, value) {
			return /^([01]?[0-9]|2[0-3]):[0-5][0-9]$/.test(value || '') ? true : _('Enter a time like 07:00');
		};

		o = s.option(form.MultiValue, 'days', _('Days'));
		[
			[ 'mon', _('Mon') ], [ 'tue', _('Tue') ], [ 'wed', _('Wed') ], [ 'thu', _('Thu') ],
			[ 'fri', _('Fri') ], [ 'sat', _('Sat') ], [ 'sun', _('Sun') ]
		].forEach(function(d) { o.value(d[0], d[1]); });
		o.rmempty = false;

		o = s.option(form.Value, 'seconds', _('Ring for (seconds)'));
		o.datatype = 'range(10,120)';
		o.placeholder = '60';

		return m.render();
	}
});
