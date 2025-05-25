/* Javascript code that will enable quick filtering of items in tables.
**
** Add an input field with the id 'quickfilter' as follows:
**   <input type="search" id="quickfilter" style="display: none"
**    placeholder="filter list...">
** The id on the input-field is important for the script. The input type
** can be text or search, with search some browsers add functionality to
** clear the field. The display: none is added to hide it from users that
** haven't enabled Javascript, as the script to make it visible is never
** executed. This is because without Javascript this input-field would be
** functionless.
**
** Mark the table with the filter items with the class 'filterable'.
** The table is expected to have a tbody around the rows that are
** filtered (to avoid filtering the header).
**
** The user can type to filter the table for elements matching the typed text.
*/

const quickfilter = document.getElementById('quickfilter');

document.addEventListener('DOMContentLoaded', function(){
  quickfilter.style.display = '';
});

quickfilter.addEventListener('input', function(){
  const filterrows = document.querySelectorAll('.filterable tbody tr');
  const filter = quickfilter.value.toLowerCase().trim();
  let group = null;
  let groupmatched = false;
  for(row of filterrows){
    const orig = row.innerHTML;
    const cleaned = orig.replaceAll("<mark>", "").replaceAll("</mark>", "");
    if(filter===''){
      row.innerHTML = cleaned;
      row.style.display = 'table-row';
      continue;
    }
    if (row.classList.contains("separator")){
      group = [];
      groupmatched = false;
    }
    let ind = cleaned.toLowerCase().lastIndexOf(filter);
    if(ind<0 && !groupmatched){
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
    row.style.display =
      (marked===cleaned && !groupmatched) ? 'none' : 'table-row';
    row.innerHTML = marked;
    if (marked!=cleaned && group){
      if (!groupmatched)
        for (grouprow of group) grouprow.style.display = 'table-row';
      groupmatched = true;
    }
    if (group) group.push(row);
  };
});
