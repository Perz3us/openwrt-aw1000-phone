'use strict';
'require view';
'require fxs.tabs';
'require form';
'require rpc';
'require uci';

var callRec = rpc.declare({ object: 'fxs', method: 'rec_status' });

function mb(bytes) {
	return (bytes / 1048576).toFixed(bytes < 10485760 ? 1 : 0) + ' MB';
}

function digits(n) {
	return (n || '').replace(/[^0-9]/g, '').slice(-9);
}

return view.extend({
	load: function() {
		return Promise.all([ uci.load('fxs'), L.resolveDefault(callRec(), {}) ]);
	},

	where: function(st) {
		var lines = [], files = st.files || [], kept = files.filter(function(f) { return f.keep; }).length;

		if (st.usb_present)
			lines.push(_('Saving to the USB stick. %s free on it.').format(mb((st.usb_free_mb || 0) * 1048576)));
		else
			lines.push(_('No USB stick found, saving to the router\'s own storage. %s free there.').format(mb((st.flash_free_mb || 0) * 1048576)));
		if (st.limit)
			lines.push(_('%s of %s used by recordings, %d recordings, %d kept.').format(mb(st.used || 0), mb(st.limit), files.length, kept));
		return E('div', {}, lines.map(function(l) { return E('p', {}, l); }));
	},

	warnings: function(st) {
		var w = [];

		if (st.full)
			w.push(_('The last call was not recorded, or was cut short, because the space ran out. Delete recordings, unmark some kept ones, or raise the limit.'));
		if (st.pruned)
			w.push(_('Old recordings were removed to make room for new ones (%d so far since the phone service started).').format(st.pruned));
		return w.map(function(t) { return E('div', { 'class': 'alert-message warning' }, t); });
	},

	render: function(data) {
		var st = data[1] || {}, m, s, o;

		m = new form.Map('fxs', _('Call recording'),
			_('Saves answered calls on the RJ11 phone and on SIP phones, so you can play them back from the call history. Calls that were not answered are not recorded. The caller is on the left side and your phone on the right.'));

		s = m.section(form.NamedSection, 'main', 'fxs');
		s.description = E('div', {}, [ this.where(st) ].concat(this.warnings(st)));

		o = s.option(form.ListValue, 'rec_mode', _('Record'),
			_('The numbers below stay saved whichever you pick, so you can switch and come back. Untick a number to pause it without removing it.'));
		o.value('off', _('Off'));
		o.value('every', _('All calls'));
		o.value('all', _('All calls except Never record'));
		o.value('chosen', _('Only chosen numbers (Always record)'));
		o.default = 'off';

		o = s.option(form.ListValue, 'rec_format', _('Sound'));
		o.value('stereo', _('Each side separate, about 2 MB a minute'));
		o.value('mono', _('Both sides mixed, about 1 MB a minute'));
		o.default = 'stereo';
		o.description = _('HD calls take twice the space.');

		o = s.option(form.Value, 'rec_usb_mb', _('Space on a USB stick (MB)'),
			_('Recordings go to a phone-recordings folder on the first USB stick found. When the space is used up, the oldest recordings that are not kept are removed.'));
		o.datatype = 'range(10,1000000)';
		o.default = '1000';
		o.validate = function(section_id, value) {
			var free = st.usb_present ? (st.usb_free_mb || 0) + Math.round((st.usb ? st.used || 0 : 0) / 1048576) : 0;

			if (!/^[0-9]+$/.test(value || ''))
				return _('Enter a size in MB');
			if (free && +value > free)
				return _('The stick has only %d MB free').format(free);
			return true;
		};

		o = s.option(form.Value, 'rec_flash_mb', _('Space on the router (MB)'),
			_('Used when no USB stick is plugged in. Recordings stay after a restart, but a firmware upgrade that does not keep settings deletes them. 20 MB is always left free for the router.'));
		o.default = '50';
		o.validate = function(section_id, value) {
			var max = (st.flash_free_mb || 0) + Math.round((!st.usb ? st.used || 0 : 0) / 1048576) - 20;

			if (!/^[0-9]+$/.test(value || '') || +value < 5)
				return _('Enter a size of at least 5 MB');
			if (st.flash_free_mb != null && +value > max)
				return max >= 5 ? _('The router has room for at most %d MB').format(max) : _('The router does not have enough free space');
			return true;
		};

		s = m.section(form.GridSection, 'recnum', _('Numbers'), this.listText());
		s.anonymous = true;
		s.addremove = true;
		s.nodescriptions = true;

		o = s.option(form.Flag, 'enabled', _('On'));
		o.default = '1';
		o.rmempty = false;
		o.editable = true;

		o = s.option(form.ListValue, 'rule', _('List'));
		o.value('always', _('Always record'));
		o.value('never', _('Never record'));
		o.default = 'never';

		o = s.option(form.Value, 'number', _('Number'));
		o.rmempty = false;
		uci.sections('fxs', 'contact').forEach(function(c) {
			if (c.number)
				o.value(c.number, c.name ? '%s (%s)'.format(c.name, c.number) : c.number);
		});
		o.validate = function(section_id, value) {
			return /^\+?[0-9]{3,20}$/.test(value || '') ? true : _('Enter a phone number');
		};

		return m.render();
	},

	listText: function() {
		var seen = {}, both = [], lines;

		uci.sections('fxs', 'recnum').forEach(function(r) {
			var d = digits(r.number);

			if (r.enabled == '0')
				return;

			if (d && seen[d] && seen[d] != r.rule && both.indexOf(r.number) < 0)
				both.push(r.number);
			seen[d] = r.rule;
		});
		lines = [ E('p', {}, _('Pick a contact or type a number. Numbers match with or without the country code.')) ];
		if (both.length)
			lines.push(E('div', { 'class': 'alert-message warning' },
				_('%s is in both lists. The one lower in the table, added last, counts.').format(both.join(', '))));
		return E('div', {}, lines);
	}
});
