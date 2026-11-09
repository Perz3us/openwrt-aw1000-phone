'use strict';
'require view';
'require fxs.tabs';
'require form';

return view.extend({
	render: function() {
		var m, s, o;

		m = new form.Map('fxs', _('Phonebook'),
			_('Names show in the call history. A phone with a display also gets the name, but only with FSK caller ID and only in plain English letters, so a name in other letters sends just the number. The ring choice covers the RJ11 phone only. Numbers match with or without the country code.'));

		s = m.section(form.GridSection, 'contact');
		s.anonymous = true;
		s.addremove = true;
		s.nodescriptions = true;

		o = s.option(form.Value, 'name', _('Name'));
		o.rmempty = false;
		o.validate = function(section_id, value) {
			if (!value)
				return _('Enter a name');
			return new TextEncoder().encode(value).length <= 47 && !/[\t\r\n]/.test(value) ? true : _('This name is too long');
		};

		o = s.option(form.Value, 'number', _('Number'));
		o.placeholder = '0771234567';
		o.rmempty = false;
		o.validate = function(section_id, value) {
			return /^\+?[0-9]{3,20}$/.test(value || '') ? true : _('Enter a phone number');
		};

		o = s.option(form.ListValue, 'ring', _('Ring'));
		o.value('default', _('Same as Settings'));
		o.value('standard', _('Standard (2 s ring, 4 s pause)'));
		o.value('short', _('Short (1 s ring, 4 s pause)'));
		o.value('double', _('Double ring'));
		o.default = 'default';

		o = s.option(form.ListValue, 'priority', _('Do not disturb'));
		o.value('normal', _('Follow the windows'));
		o.value('important', _('Important, always rings'));
		o.value('never', _('Never ring, only show in the history'));
		o.default = 'normal';

		return m.render();
	}
});
