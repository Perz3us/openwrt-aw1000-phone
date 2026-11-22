'use strict';
'require baseclass';
'require rpc';

var callBadges = rpc.declare({ object: 'fxs', method: 'badges' });

function newer(a, b) {
	var x = (a || '').split(/[.-]/), y = (b || '').split(/[.-]/), i;

	for (i = 0; i < 3; i++)
		if ((+x[i] || 0) != (+y[i] || 0))
			return (+x[i] || 0) > (+y[i] || 0);
	return false;
}

function markTab(name, text, tries) {
	var a = document.querySelector('#tabmenu .tabs > li.tabmenu-item-' + name + ' > a'),
	    o = document.querySelector('.fxs-nav option[value$="/fxs/' + name + '"]');

	if (!a || !o) {
		if (tries > 0)
			window.setTimeout(function() { markTab(name, text, tries - 1); }, 150);
		return;
	}
	if (a.parentNode.classList.contains('active') || a.querySelector('.fxs-badge'))
		return;
	a.appendChild(E('span', { 'class': 'fxs-badge' }, text));
	o.textContent += ' (' + text + ')';
}

function showActive(tries) {
	var li = document.querySelector('#tabmenu .tabs > li.active'),
	    ul = li && li.parentNode,
	    a, b;

	if (!li || !ul.clientWidth) {
		if (tries > 0)
			window.setTimeout(function() { showActive(tries - 1); }, 100);
		return;
	}
	if (ul.scrollWidth <= ul.clientWidth)
		return;
	a = li.getBoundingClientRect();
	b = ul.getBoundingClientRect();
	ul.scrollLeft += a.left - b.left - (b.width - a.width) / 2;
}

function addNav(tries) {
	var menu = document.getElementById('tabmenu'),
	    links = menu ? menu.querySelectorAll('.tabs > li > a') : [],
	    nav;

	if (!links.length) {
		if (tries > 0)
			window.setTimeout(function() { addNav(tries - 1); }, 100);
		return;
	}
	if (menu.querySelector('.fxs-nav'))
		return;
	nav = E('select', { 'class': 'fxs-nav', 'aria-label': _('Page'), 'change': function() { location.href = this.value; } },
		Array.prototype.map.call(links, function(a) {
			return E('option', { 'value': a.getAttribute('href'), 'selected': a.parentNode.classList.contains('active') ? '' : null },
				a.textContent);
		}));
	menu.insertBefore(E('div', { 'class': 'fxs-navwrap' }, nav), menu.firstChild);
}

var css = '\
.fxs-navwrap { display: none; }.fxs-badge { display: inline-block; margin-left: 6px; padding: 0 7px; border-radius: 999px; background: #e5534b; color: #fff; font-size: 11px; font-weight: 700; line-height: 18px; }\
.fxs-actions { display: flex; flex-wrap: wrap; gap: 8px; }\
@media (max-width: 600px) {\
.fxs-app #tabmenu > .tabs { display: none !important; }\
.fxs-app .fxs-navwrap { display: block; position: relative; }\
.fxs-app .fxs-navwrap::after { content: "\\25BE"; position: absolute; right: 16px; top: 50%; transform: translateY(-50%); pointer-events: none; font-size: 16px; opacity: .8; }\
.fxs-app .fxs-nav { display: block; width: 100%; height: 46px; padding: 0 40px 0 14px; font-size: 16px; font-weight: 600; border-radius: 12px; border: 1px solid rgba(128,128,128,.35); background-image: none !important; }\
.fxs-app #view { gap: 12px; }\
.fxs-app #view > h2, .fxs-app #view > div > h2 { font-size: 18px; padding: 12px 16px; border-radius: 12px; }\
.fxs-app #view .cbi-map-descr { padding: 0 16px; font-size: 13px; line-height: 1.5; }\
.fxs-app #view .cbi-section { border-radius: 12px; border: 1px solid rgba(128,128,128,.2); overflow: hidden; margin: 0; }\
.fxs-app #view .cbi-section:not([id]) { padding-bottom: 16px; }\
.fxs-app #view .cbi-section:not([id]) > h3 { font-size: 16px; padding: 12px 16px; margin: 0 0 12px; }\
.fxs-app #view .cbi-section:not([id]) > :not(h3) { margin-left: 16px; margin-right: 16px; }\
.fxs-app #view .cbi-section:not([id]) > :first-child:not(h3) { margin-top: 16px; }\
.fxs-app #view .cbi-section:not([id]) > p { padding: 0; margin-top: 0; margin-bottom: 12px; }\
.fxs-app #view .cbi-section:not([id]) > .table { width: auto; }\
.fxs-app #view .cbi-value-description { font-size: 12.5px; line-height: 1.45; }\
.fxs-app #view .cbi-section-node-tabbed, .fxs-app #view .cbi-section-node-tabbed > div { margin-left: 0 !important; margin-right: 0 !important; }\
.fxs-app #view .cbi-section:not([id]) > h3:not(:first-child) { margin-top: 16px; }\
.fxs-app #view .cbi-section:not([id]) > .cbi-map-descr { padding: 0 !important; margin-bottom: 12px; }\
.fxs-app #view .cbi-section:not([id]) > .alert-message { width: auto !important; box-sizing: border-box; border-radius: 10px; margin: 0 16px 12px !important; left: 0 !important; transform: none !important; }\
.fxs-table .td:has(> input[type="radio"]) { align-self: flex-start; padding-top: 4px !important; }\
.fxs-table .td > input[type="radio"] { margin: 0 !important; vertical-align: top; position: relative; top: -3px; }\
.fxs-app #view .cbi-section:not([id]) > .cbi-section-descr { padding: 0 !important; margin-bottom: 12px; font-size: 13px; line-height: 1.5; opacity: .85; }\
.fxs-app #view input:not([type="checkbox"]):not([type="radio"]), .fxs-app #view select { font-size: 16px; min-height: 42px; }\
.fxs-app #view .btn, .fxs-app .cbi-page-actions .btn, .fxs-app #view .cbi-button { min-height: 42px; border-radius: 10px; }\
.fxs-actions > .btn { flex: 1 1 0; margin: 0 !important; }\
.fxs-app .cbi-page-actions { position: sticky; bottom: 0; z-index: 20; display: flex; flex-wrap: wrap; justify-content: flex-end; gap: 8px; padding: 10px 16px !important; margin: 0 -1px; background: var(--fxs-bg); border-top: 1px solid rgba(128,128,128,.25); }\
.fxs-table, .fxs-kv { font-size: 14px !important; }\
.fxs-table .tr { padding: 8px 0; column-gap: 12px; border-bottom: 1px solid rgba(128,128,128,.15); }\
.fxs-table .tr:last-child { border-bottom: 0; }\
.fxs-table .td { flex: 1 1 40% !important; width: auto !important; text-align: left !important; padding: 4px 0 !important; border: 0 !important; background: transparent !important; }\
.fxs-table .td:not([data-title]) { flex-basis: 100% !important; }\
.fxs-table .td:has(> input[type="radio"]) { flex: 0 0 auto !important; padding-right: 10px !important; }\
.fxs-table .td:has(> input[type="radio"]) + .td { flex: 1 1 80% !important; }\
.fxs-table .td:empty { display: none !important; }\
.fxs-table .td[data-title]::before { display: block; font-weight: normal; font-size: 12px; opacity: .65; padding: 0 0 2px; }\
.fxs-table .td input.cbi-input-text { width: 100%; }\
.fxs-table .td .btn { margin: 2px 6px 2px 0; min-height: 36px; padding: 0 14px; }\
.fxs-kv .tr, .fxs-table .tr { background: transparent !important; }\
.fxs-kv .tr { border-bottom: 1px solid rgba(128,128,128,.15); }\
.fxs-kv .tr:last-child { border-bottom: 0; }\
.fxs-kv .td:empty { display: none !important; }\
.fxs-kv .td { flex: 1 1 50% !important; width: auto !important; text-align: left !important; padding: 9px 0 !important; border: 0 !important; background: transparent !important; }\
.fxs-kv .td:first-child { flex: 0 0 42% !important; opacity: .7; padding-right: 10px !important; }\
.fxs-kv .td:nth-child(2):not(:last-child) { flex: 0 1 auto !important; padding-right: 10px !important; }\
.fxs-kv .td:nth-child(3) { flex: 1 1 auto !important; opacity: .8; }\
#view input.cbi-input-text, #view select, #view textarea { max-width: 100%; }\
#view .cbi-dynlist { max-width: 100%; }\
#view .cbi-dynlist input { min-width: 0; }\
#view .cbi-value-field { width: 100% !important; }\
#view .cbi-value-field > input:not([type="checkbox"]):not([type="radio"]), #view .cbi-value-field > select, #view .cbi-value-field > div > input:not([type="checkbox"]):not([type="radio"]), #view .cbi-value-field > div > select, #view .cbi-value-field .cbi-dropdown { width: 100% !important; }\
#view .cbi-section-table .td { text-align: left !important; padding-left: 16px !important; }\
#view .cbi-section-table .tr.placeholder .td { text-align: center !important; }\
#view .cbi-section-table .td[data-title]::before { display: inline !important; padding-right: 6px !important; }\
#view .cbi-section-table .td.cbi-section-actions > div { justify-content: flex-start !important; }\
#view .fxs-wrap { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }\
.fxs-app .modal input.cbi-input-text { font-size: 16px; min-height: 42px; }\
.fxs-app .modal .right { display: flex; gap: 8px; }\
.fxs-app .modal .right > .btn { flex: 1 1 0; min-height: 42px; margin: 0; }\
}';

return baseclass.extend({
	__init__: function() {
		if (!document.getElementById('fxs-mobile'))
			document.head.appendChild(E('style', { 'id': 'fxs-mobile' }, css));
		document.body.classList.add('fxs-app');
		document.body.style.setProperty('--fxs-bg', getComputedStyle(document.body).backgroundColor);
		addNav(30);
		showActive(30);
		L.resolveDefault(callBadges(), {}).then(function(b) {
			if (b.missed > 0)
				markTab('history', String(b.missed), 30);
			if (b.latest && newer(b.latest, b.installed))
				markTab('status', _('update'), 30);
		});
	},

	newer: newer,
	badges: callBadges
});
