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

quickfilter.addEventListener('input', function(){
  const quickfilter = document.getElementById('quickfilter');
  const filterlist = document.querySelectorAll('.filterlist tbody tr');
  const filter = quickfilter.value.toLowerCase().trim();
  for(row of filterlist){
    const orig = row.innerHTML;
    const cleaned = orig.replaceAll("<mark>", "").replaceAll("</mark>", "");
    if(filter===''){
      row.innerHTML = cleaned;
      row.style.display = 'table-row';
      continue;
    }
    let ind = cleaned.toLowerCase().lastIndexOf(filter);
    if(ind<0){
      row.innerHTML = cleaned;
      row.style.display = 'none';
    }
    let marked = cleaned;
    do{
      if(cleaned.lastIndexOf('<',ind-1)<cleaned.lastIndexOf('>',ind-1)){
        // not inside a tag
        marked = marked.substring(0,ind)+'<mark>'+
          marked.substring(ind, ind+filter.length)+'</mark>'+
          marked.substring(ind+filter.length);
      }
      ind = cleaned.toLowerCase().lastIndexOf(filter,ind-1);
    }while(ind>=0);
    row.style.display = (marked===cleaned) ? 'none' : 'table-row';
    row.innerHTML = marked;
  };
});
