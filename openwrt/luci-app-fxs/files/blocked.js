'use strict';
'require view';
'require form';

return view.extend({
	render: function() {
		var m, s, o;

		m = new form.Map('fxs', _('Blocked numbers'),
			_('Calls from these numbers are rejected without ringing the phone. Numbers match with or without the country code.'));

		s = m.section(form.NamedSection, 'main', 'fxs');

		o = s.option(form.Flag, 'block_private', _('Block hidden numbers'),
			_('Reject calls that do not show a number.'));
		o.rmempty = false;

		s = m.section(form.GridSection, 'block');
		s.anonymous = true;
		s.addremove = true;
		s.nodescriptions = true;

		o = s.option(form.Value, 'name', _('Name'));

		o = s.option(form.Value, 'number', _('Number'));
		o.placeholder = '0771234567';
		o.rmempty = false;
		o.validate = function(section_id, value) {
			return /^\+?[0-9]{3,20}$/.test(value || '') ? true : _('Enter a phone number');
		};

		return m.render();
	}
});
