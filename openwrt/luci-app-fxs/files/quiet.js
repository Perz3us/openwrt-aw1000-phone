'use strict';
'require view';
'require fxs.tabs';
'require form';
'require rpc';
'require uci';

var callStatus = rpc.declare({ object: 'fxs', method: 'status' });

return view.extend({
	load: function() {
		return Promise.all([ callStatus(), uci.load('fxs') ]).then(function(r) { return r[0]; });
	},

	who: function(s) {
		var o = s.option(form.ListValue, 'mode', _('Who can ring'));

		o.value('repeat', _('Important contacts, the Always ring list and repeat callers'));
		o.value('allowed', _('Important contacts and the Always ring list'));
		o.value('all', _('Important contacts only'));
		o.default = s.sectiontype == 'holiday' ? 'allowed' : 'repeat';
	},

	act: function(s) {
		var o = s.option(form.ListValue, 'action', _('Other calls'));

		o.value('silent', _('Stay silent'));
		o.value('once', _('One short ring'));
		o.value('busy', _('Caller hears busy'));
		o.default = 'silent';
	},

	ended: function() {
		var today = new Date(), names = [];

		today = today.getFullYear() * 10000 + (today.getMonth() + 1) * 100 + today.getDate();
		uci.sections('fxs', 'holiday').forEach(function(h) {
			var last = (h.to_date || h.from_date || '').replace(/-/g, '');

			if (last && +last < today)
				names.push(h.name || h.from_date);
		});
		return names;
	},

	render: function(st) {
		var m, s, o;

		m = new form.Map('fxs', _('Do not disturb'),
			_('The RJ11 phone does not ring during these windows. Calls still show in the call history, with the reason. SIP phones still ring. Emergency numbers always ring, and so do contacts marked Important. A window that runs past midnight belongs to the day it starts. For a quick break with no window, use the Quiet button on the Status page or dial %s*60 for 60 minutes.').format(uci.get('fxs', 'main', 'code_dnd_on') || '*78'));

		s = m.section(form.NamedSection, 'main', 'fxs');
		s.description = st && st.dnd ? _('Do not disturb is on now.') : _('Do not disturb is off now.');

		o = s.option(form.DynamicList, 'dnd_allow', _('Always ring for'),
			_('These numbers ring when a window allows them, and when you turn do not disturb on from the phone.'));
		o.placeholder = '0771234567';
		o.validate = function(section_id, value) {
			return !value || /^\+?[0-9]{3,20}$/.test(value) ? true : _('Enter a phone number');
		};

		o = s.option(form.Value, 'dnd_repeat', _('Ring on a repeat call within (minutes)'),
			_('In a window with repeat callers on, the same number calling again within this time rings. 0 turns this off.'));
		o.datatype = 'range(0,60)';
		o.placeholder = '3';

		[
			[ 'code_dnd_on', _('Do not disturb on code'), _('Dial this on the phone to turn do not disturb on until you turn it off.'), '*78', 'code_dnd_off', '*79' ],
			[ 'code_dnd_off', _('Do not disturb off code'), _('Dial this on the phone to go back to the windows below.'), '*79', 'code_dnd_on', '*78' ]
		].forEach(function(c) {
			o = s.option(form.Value, c[0], c[1], c[2] + ' ' + _('A * and 1 to 6 digits. A speed dial with the same code goes first.'));
			o.placeholder = c[3];
			o.validate = function(section_id, value) {
				if (!value)
					return true;
				if (!/^\*[0-9]{1,6}$/.test(value))
					return _('Enter a * and 1 to 6 digits, like %s').format(c[3]);
				if (value == '*86' ||
				    value == (uci.get('fxs', 'main', 'code_redial') || '*66') ||
				    value == (uci.get('fxs', 'main', 'code_callback') || '*69') ||
				    value == (this.section.formvalue(section_id, c[4]) || c[5]))
					return _('The phone already uses this code');
				return true;
			};
		});

		s = m.section(form.GridSection, 'quiet', _('Windows'), _('Up to 32 windows and holidays are used in all.'));
		s.anonymous = true;
		s.addremove = true;
		s.nodescriptions = true;

		o = s.option(form.Flag, 'enabled', _('On'));
		o.default = '1';
		o.rmempty = false;
		o.editable = true;

		o = s.option(form.Value, 'name', _('Name'));

		o = s.option(form.MultiValue, 'days', _('Days'));
		[
			[ 'mon', _('Mon') ], [ 'tue', _('Tue') ], [ 'wed', _('Wed') ], [ 'thu', _('Thu') ],
			[ 'fri', _('Fri') ], [ 'sat', _('Sat') ], [ 'sun', _('Sun') ]
		].forEach(function(d) { o.value(d[0], d[1]); });
		o.rmempty = false;

		[ [ 'from', _('From'), '22:00' ], [ 'to', _('Until'), '07:00' ] ].forEach(function(t) {
			o = s.option(form.Value, t[0], t[1], _('Use the same time in both for the whole day.'));
			o.placeholder = t[2];
			o.rmempty = false;
			o.validate = function(section_id, value) {
				return /^([01]?[0-9]|2[0-3]):[0-5][0-9]$/.test(value || '') ? true : _('Enter a time like 22:00');
			};
		});

		this.who(s);
		this.act(s);

		var notes = [ E('p', {}, _('A holiday date replaces the weekly windows for that day. Leave both times at 00:00 for the whole day.')) ],
		    ended = this.ended();

		if (uci.get('fxs', 'main', 'sip_enabled') == '1')
			notes.push(E('div', { 'class': 'alert-message' }, _('SIP calling is on, so Caller hears busy works like Stay silent, letting SIP phones still ring.')));
		if (ended.length)
			notes.push(E('div', { 'class': 'alert-message' }, _('Over and can be removed: %s').format(ended.join(', '))));

		s = m.section(form.GridSection, 'holiday', _('Holidays'), E('div', {}, notes));
		s.anonymous = true;
		s.addremove = true;
		s.nodescriptions = true;

		o = s.option(form.Flag, 'enabled', _('On'));
		o.default = '1';
		o.rmempty = false;
		o.editable = true;

		o = s.option(form.Value, 'name', _('Name'));
		o.placeholder = _('New Year');

		[ [ 'from_date', _('First day') ], [ 'to_date', _('Last day') ] ].forEach(function(t) {
			o = s.option(form.Value, t[0], t[1]);
			o.placeholder = '2026-12-31';
			o.rmempty = t[0] == 'to_date';
			o.validate = function(section_id, value) {
				if (!value && t[0] == 'to_date')
					return true;
				return /^20[0-9]{2}-(0[1-9]|1[0-2])-(0[1-9]|[12][0-9]|3[01])$/.test(value || '') ? true : _('Enter a date like 2026-12-31');
			};
		});

		[ [ 'from', _('From') ], [ 'to', _('Until') ] ].forEach(function(t) {
			o = s.option(form.Value, t[0], t[1]);
			o.placeholder = '00:00';
			o.validate = function(section_id, value) {
				return !value || /^([01]?[0-9]|2[0-3]):[0-5][0-9]$/.test(value) ? true : _('Enter a time like 22:00');
			};
		});

		this.who(s);
		this.act(s);

		return m.render();
	}
});
