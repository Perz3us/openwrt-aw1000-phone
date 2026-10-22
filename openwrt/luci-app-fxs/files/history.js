'use strict';
'require view';
'require rpc';
'require uci';
'require ui';
'require dom';

var callStatus = rpc.declare({ object: 'fxs', method: 'status' });
var callHistory = rpc.declare({ object: 'fxs', method: 'history', expect: { log: '' } });
var callClear = rpc.declare({ object: 'fxs', method: 'clear_history' });

var PAGE = 50;

var statusText = {
	answered: _('Answered'),
	missed: _('Missed'),
	blocked: _('Blocked'),
	failed: _('Failed'),
	cancelled: _('Not answered'),
	active: _('In progress')
};

var css = '\
.fxs-stats { display: flex; flex-wrap: wrap; gap: 12px; margin-bottom: 16px; }\
.fxs-stat { flex: 1 1 120px; padding: 12px 16px; border: 1px solid rgba(128,128,128,.35); border-radius: 6px; }\
.fxs-stat .n { font-size: 1.8em; font-weight: bold; line-height: 1.3; }\
.fxs-stat .l { opacity: .75; }\
.fxs-bar { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin-bottom: 12px; }\
.fxs-bar input, .fxs-bar select { width: auto; min-width: 0; }\
.fxs-pager { display: flex; gap: 8px; align-items: center; justify-content: flex-end; margin-top: 8px; }\
.fxs-in { color: #2e8b3e; }\
.fxs-out { color: #3a7bd5; }\
.fxs-bad { color: #c0392b; }';

function digits(n) {
	return (n || '').replace(/[^0-9]/g, '').slice(-9);
}

function parse(text, live) {
	var calls = [], open = {};

	function close(c, t, status) {
		c.end = t;
		c.status = status || (c.connected ? 'answered' : (c.dir == 'in' ? 'missed' : 'cancelled'));
		delete open[c.number];
	}

	function closeAll(t) {
		Object.keys(open).forEach(function(k) { close(open[k], t); });
	}

	function start(t, num, dir) {
		var c = { start: t, number: num, dir: dir, connected: null, end: null, status: null };

		if (open[num])
			close(open[num], null);
		calls.push(c);
		open[num] = c;
		return c;
	}

	(text || '').split('\n').forEach(function(line) {
		var m = line.match(/^(\d+) (\S+) ?(.*)$/), t, ev, num, c;

		if (!m)
			return;
		t = +m[1];
		ev = m[2];
		num = m[3].trim();
		c = open[num];

		switch (ev) {
		case 'dial':
			closeAll(null);
			start(t, num, 'out');
			break;
		case 'incoming':
			closeAll(null);
			start(t, num, 'in');
			break;
		case 'waiting':
			start(t, num, 'in');
			break;
		case 'blocked':
			calls.push({ start: t, number: num, dir: 'in', connected: null, end: t, status: 'blocked' });
			break;
		case 'connected':
		case 'answered':
		case 'switched':
		case 'retrieved':
			if (!c)
				c = start(t, num, ev == 'connected' ? 'out' : 'in');
			if (!c.connected)
				c.connected = t;
			break;
		case 'failed':
		case 'missed':
			if (c)
				close(c, t, ev);
			break;
		case 'hangup':
			if (c)
				close(c, t);
			break;
		case 'ended':
			if (c)
				close(c, t);
			closeAll(t);
			break;
		}
	});

	Object.keys(open).forEach(function(k) {
		if (live)
			open[k].status = 'active';
		else
			close(open[k], null);
	});

	return calls.reverse();
}

function duration(c) {
	var s, h, m;

	if (!c.connected || !c.end || c.end < c.connected)
		return '-';
	s = c.end - c.connected;
	h = Math.floor(s / 3600);
	m = Math.floor(s / 60) % 60;
	s = s % 60;
	return (h ? h + ':' + (m < 10 ? '0' : '') : '') + m + ':' + (s < 10 ? '0' : '') + s;
}

function dayStart(v) {
	var p = (v || '').split('-');

	return p.length == 3 ? new Date(+p[0], +p[1] - 1, +p[2]).getTime() / 1000 : null;
}

function isoDay(d) {
	var m = d.getMonth() + 1, day = d.getDate();

	return d.getFullYear() + '-' + (m < 10 ? '0' : '') + m + '-' + (day < 10 ? '0' : '') + day;
}

function isMissed(c) {
	return c.dir == 'in' && c.status == 'missed';
}

return view.extend({
	load: function() {
		return Promise.all([
			callHistory(),
			callStatus().catch(function() { return {}; }),
			uci.load('fxs')
		]);
	},

	names: function() {
		var map = {};

		[ 'speed', 'block' ].forEach(function(type) {
			uci.sections('fxs', type).forEach(function(s) {
				if (s.name && s.number)
					map[digits(s.number)] = s.name;
			});
		});
		return map;
	},

	filtered: function(withType) {
		var from = dayStart(this.from.value),
		    to = dayStart(this.to.value),
		    q = this.search.value.replace(/[^0-9+]/g, ''),
		    qn = this.search.value.trim().toLowerCase(),
		    type = this.type.value,
		    names = this.nameMap;

		return this.calls.filter(function(c) {
			if (from != null && c.start < from)
				return false;
			if (to != null && c.start >= to + 86400)
				return false;
			if (qn && !(q && c.number.indexOf(q) >= 0) &&
			    !((names[digits(c.number)] || '').toLowerCase().indexOf(qn) >= 0))
				return false;
			if (!withType || type == 'all')
				return true;
			if (type == 'missed')
				return isMissed(c);
			if (type == 'blocked')
				return c.status == 'blocked';
			return c.dir == type && c.status != 'blocked';
		});
	},

	refresh: function() {
		var all = this.filtered(false),
		    rows = this.filtered(true),
		    pages = Math.max(1, Math.ceil(rows.length / PAGE)),
		    names = this.nameMap,
		    count = function(f) { return all.filter(f).length; };

		if (this.page >= pages)
			this.page = pages - 1;

		dom.content(this.stats, [
			[ _('Total calls'), all.length ],
			[ _('Incoming'), count(function(c) { return c.dir == 'in' && c.status != 'blocked'; }) ],
			[ _('Outgoing'), count(function(c) { return c.dir == 'out'; }) ],
			[ _('Missed'), count(isMissed) ],
			[ _('Blocked'), count(function(c) { return c.status == 'blocked'; }) ]
		].map(function(s) {
			return E('div', { 'class': 'fxs-stat' }, [
				E('div', { 'class': 'n' }, String(s[1])),
				E('div', { 'class': 'l' }, s[0])
			]);
		}));

		var head = E('tr', { 'class': 'tr table-titles' }, [
			E('th', { 'class': 'th' }, _('Date and time')),
			E('th', { 'class': 'th' }, _('Number')),
			E('th', { 'class': 'th' }, _('Type')),
			E('th', { 'class': 'th' }, _('Duration')),
			E('th', { 'class': 'th' }, _('Status'))
		]);

		var body = rows.slice(this.page * PAGE, (this.page + 1) * PAGE).map(function(c) {
			var name = names[digits(c.number)],
			    bad = c.status == 'missed' || c.status == 'blocked' || c.status == 'failed';

			return E('tr', { 'class': 'tr' }, [
				E('td', { 'class': 'td' }, new Date(c.start * 1000).toLocaleString()),
				E('td', { 'class': 'td' }, name ? [ E('strong', name), E('br'), c.number ] : (c.number || _('Private number'))),
				E('td', { 'class': 'td' }, E('span', { 'class': c.dir == 'in' ? 'fxs-in' : 'fxs-out' },
					c.dir == 'in' ? '↙ ' + _('Incoming') : '↗ ' + _('Outgoing'))),
				E('td', { 'class': 'td' }, duration(c)),
				E('td', { 'class': 'td' }, E('span', { 'class': bad ? 'fxs-bad' : '' }, statusText[c.status] || c.status))
			]);
		});

		if (!body.length)
			body = [ E('tr', { 'class': 'tr placeholder' }, E('td', { 'class': 'td', 'colspan': 5 }, E('em', _('No calls match the filters.')))) ];

		dom.content(this.table, [ head ].concat(body));

		this.prev.disabled = this.page <= 0;
		this.next.disabled = this.page >= pages - 1;
		dom.content(this.pageInfo, _('%d calls, page %d of %d').format(rows.length, this.page + 1, pages));
	},

	setRange: function(days) {
		var now = new Date();

		this.to.value = days == null ? '' : isoDay(now);
		this.from.value = days == null ? '' : isoDay(new Date(now.getFullYear(), now.getMonth(), now.getDate() - days + 1));
		this.page = 0;
		this.refresh();
	},

	exportCsv: function() {
		var names = this.nameMap,
		    lines = [ [ 'Date', 'Time', 'Number', 'Name', 'Type', 'Duration', 'Status' ].join(',') ];

		this.filtered(true).forEach(function(c) {
			var d = new Date(c.start * 1000);

			lines.push([
				isoDay(d), d.toTimeString().slice(0, 8), c.number, names[digits(c.number)] || '',
				c.dir == 'in' ? 'Incoming' : 'Outgoing', duration(c), c.status
			].map(function(v) { return '"' + String(v).replace(/"/g, '""') + '"'; }).join(','));
		});

		var a = E('a', {
			'href': URL.createObjectURL(new Blob([ lines.join('\r\n') + '\r\n' ], { type: 'text/csv' })),
			'download': 'call-history-' + isoDay(new Date()) + '.csv'
		});
		document.body.appendChild(a);
		a.click();
		document.body.removeChild(a);
	},

	clearHistory: function() {
		ui.showModal(_('Clear call history'), [
			E('p', _('This deletes every call in the history. It cannot be undone.')),
			E('div', { 'class': 'right' }, [
				E('button', { 'class': 'btn', 'click': ui.hideModal }, _('Cancel')),
				' ',
				E('button', { 'class': 'btn cbi-button-negative', 'click': L.bind(function() {
					return callClear().then(L.bind(function() {
						ui.hideModal();
						this.calls = [];
						this.page = 0;
						this.refresh();
					}, this));
				}, this) }, _('Delete all'))
			])
		]);
	},

	render: function(data) {
		var st = data[1] || {},
		    live = [ 'calling', 'talking', 'ringing' ].indexOf(st.state) >= 0,
		    update = L.bind(function() { this.page = 0; this.refresh(); }, this);

		this.calls = parse(data[0], live);
		this.nameMap = this.names();
		this.page = 0;

		this.from = E('input', { 'type': 'date', 'class': 'cbi-input-text', 'change': update });
		this.to = E('input', { 'type': 'date', 'class': 'cbi-input-text', 'change': update });
		this.search = E('input', { 'type': 'search', 'class': 'cbi-input-text', 'placeholder': _('Search number or name'), 'input': update });
		this.type = E('select', { 'class': 'cbi-input-select', 'change': update }, [
			E('option', { 'value': 'all' }, _('All calls')),
			E('option', { 'value': 'in' }, _('Incoming')),
			E('option', { 'value': 'out' }, _('Outgoing')),
			E('option', { 'value': 'missed' }, _('Missed')),
			E('option', { 'value': 'blocked' }, _('Blocked'))
		]);
		this.stats = E('div', { 'class': 'fxs-stats' });
		this.table = E('table', { 'class': 'table' });
		this.pageInfo = E('span');
		this.prev = E('button', { 'class': 'btn', 'click': L.bind(function() { this.page--; this.refresh(); }, this) }, '‹ ' + _('Newer'));
		this.next = E('button', { 'class': 'btn', 'click': L.bind(function() { this.page++; this.refresh(); }, this) }, _('Older') + ' ›');

		var range = function(label, days) {
			return E('button', { 'class': 'btn cbi-button', 'click': L.bind(this.setRange, this, days) }, label);
		}.bind(this);

		var view = E([], [
			E('style', css),
			E('h2', _('Call history')),
			E('div', { 'class': 'cbi-map-descr' }, _('Calls made and received on the phone port. Names come from your speed dial and blocked numbers.')),
			E('div', { 'class': 'cbi-section' }, [
				this.stats,
				E('div', { 'class': 'fxs-bar' }, [
					range(_('Today'), 1),
					range(_('7 days'), 7),
					range(_('30 days'), 30),
					range(_('All'), null),
					E('span', _('From')), this.from,
					E('span', _('To')), this.to
				]),
				E('div', { 'class': 'fxs-bar' }, [
					this.search,
					this.type,
					E('button', { 'class': 'btn cbi-button-action', 'click': L.bind(this.exportCsv, this) }, _('Export CSV')),
					E('button', { 'class': 'btn cbi-button-negative', 'click': L.bind(this.clearHistory, this) }, _('Clear history'))
				]),
				this.table,
				E('div', { 'class': 'fxs-pager' }, [ this.pageInfo, this.prev, this.next ])
			])
		]);

		this.refresh();
		return view;
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
