/* Javascript code that will enable quick filtering of items in tables.
**
** Add an input field with the id 'quickfilter' as follows:
**   <input type="text" id="quickfilter" placeholder="filter list...">
** Mark the table with the filter items with the class 'filterlist'.
** The table is expected to have a tbody around the rows that are
** filtered (to avoid filtering the header).
**
** The user can type to filter the table for elements matching the typed text.
*/

quickfilter.addEventListener('input', function (){
  const quickfilter = document.getElementById('quickfilter');
  const filterlist = document.querySelectorAll('.filterlist tbody tr');
  const filter = quickfilter.value.toLowerCase().trim();
  filterlist.forEach(function(row){
    const rowText = row.textContent.toLowerCase().trim();
    row.style.display = rowText.includes(filter) ? 'table-row' : 'none';
  });
});
