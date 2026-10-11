'use strict';
'require view';
'require form';

return view.extend({
	render: function() {
		var m, s, o;

		m = new form.Map('fxs', _('Speed dial'),
			_('Dial * and the code on the phone, then press # or wait. For example *1 calls the number saved under code 1.'));

		s = m.section(form.GridSection, 'speed');
		s.anonymous = true;
		s.addremove = true;
		s.nodescriptions = true;

		o = s.option(form.Value, 'name', _('Name'));
		o.placeholder = _('Home');

		o = s.option(form.Value, 'code', _('Code'));
		o.datatype = 'and(uinteger,maxlength(3))';
		o.rmempty = false;
		o.validate = function(section_id, value) {
			var dup = false;

			if (!value)
				return _('Enter a code');
			this.section.cfgsections().forEach(L.bind(function(sid) {
				if (sid != section_id && this.formvalue(sid) == value)
					dup = true;
			}, this));
			return dup ? _('This code is already used') : true;
		};

		o = s.option(form.Value, 'number', _('Number'));
		o.placeholder = '0771234567';
		o.rmempty = false;
		o.validate = function(section_id, value) {
			return /^\+?[0-9]{3,20}$/.test(value || '') ? true : _('Enter a phone number');
		};

		return m.render();
	}
});
