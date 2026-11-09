'use strict';
'require view';
'require fxs.tabs';
'require form';
'require uci';

return view.extend({
	load: function() {
		return uci.load('fxs');
	},

	render: function() {
		var m, s, o;

		m = new form.Map('fxs', _('Outgoing calls'),
			_('Choose which numbers the RJ11 phone can call. A number matches when it starts with a prefix in the list, and +94 counts the same as 0094. Emergency numbers always go through. SIP phones are not limited here.'));

		s = m.section(form.NamedSection, 'main', 'fxs');

		o = s.option(form.ListValue, 'out_mode', _('Rule'));
		o.value('off', _('Off, every number can be called'));
		o.value('block', _('Block the numbers in the list'));
		o.value('allow', _('Allow only the numbers in the list'));
		o.default = 'off';

		if (uci.get('fxs', 'main', 'out_mode') == 'allow' && !uci.sections('fxs', 'outgoing').length)
			s.description = E('div', { 'class': 'alert-message warning' },
				_('The list is empty, so only emergency numbers can be called.'));

		s = m.section(form.GridSection, 'outgoing');
		s.anonymous = true;
		s.addremove = true;
		s.nodescriptions = true;

		o = s.option(form.Value, 'name', _('Name'));

		o = s.option(form.Value, 'prefix', _('Number or prefix'));
		o.placeholder = '00';
		o.rmempty = false;
		o.validate = function(section_id, value) {
			return /^\+?[0-9*#]{1,20}$/.test(value || '') ? true : _('Enter a number or the first digits of one');
		};

		return m.render();
	}
});
