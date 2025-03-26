/* Javascript code that will enable quick filtering of items in tables.
** 
** Add an input field with the id 'quickfilter' as follows:
**   <input type="text" id="quickfilter" placeholder="filter list..."
**    style="display: none">
** Mark the table with the filter items with the class 'filterlist'.
** The table is expected to have a tbody around the rows that are
** filtered (to avoid filtering the header).
**
** The input field is hidden at standard ('display:none'), but the script
** will display it, if the list contains more than five elements.
**
** If shown the user can type to filter the table for elements matching
** the typed text.
*/

const quickfilter = document.getElementById('quickfilter');
const filterlist = document.querySelectorAll('.filterlist tbody tr');

document.addEventListener('DOMContentLoaded', function(){
  if (filterlist.length > 5) quickfilter.style.display = '';
});

quickfilter.addEventListener('input', function (){
  const filter = quickfilter.value.toLowerCase().trim();
  filterlist.forEach(function(row){
    const rowText = row.textContent.toLowerCase().trim();
    row.style.display = rowText.includes(filter) ? 'table-row' : 'none';
  });
});
