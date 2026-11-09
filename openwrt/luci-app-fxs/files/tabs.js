'use strict';
'require baseclass';

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

return baseclass.extend({
	__init__: function() {
		showActive(30);
	}
});
