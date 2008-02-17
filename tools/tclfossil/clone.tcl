package require http
package require zlib
package require sha1

proc login_card {userid password message} {
   # calculates the login card for the specific user for this msg

   set nonce [sha1::sha1 -hex $message]
   set signature [sha1::sha1 -hex $nonce$password]
   return "login $userid $nonce $signature\n"
}

proc http_req {url user password message} {
  set login_card [login_card $user $password $message]
  set message [blob_compress "$login_card$message"]
  return [http::geturl $url/xfer -binary 1 -query $message -type application/x-fossil]
}


proc blob_compress {data} {
    set n [string length $data]
    set data [zlib compress $data 9]
    set header [binary format I $n]
    
    return $header$data
}

proc blob_decompress {data} {
    binary scan $data I length
    return [zlib decompress [string range $data 4 end] $length ]
} 

# send buffer starts with 4 bytes (big endian) containing the length of the blob


set tok [http_req http://www.fossil-scm.org/fossil MJanssen {} clone\n]
http::wait $tok
set body [blob_decompress [http::data $tok]] 
set lines [split $body \n] 
puts $body
puts "Received:\t[string length $body] bytes,\t[llength $lines] messages"

