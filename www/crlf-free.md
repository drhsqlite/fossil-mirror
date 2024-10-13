Fossil Does Not Send CRLF
=========================

Fossil uses \\n (NL) line endings, not \\r\\n (CRLF) line endings, even for
the HTTP and SMTP protocols where CRLF line endings are required.  This is
deliberate. The founder of Fossil believes that that CRLF
line endings are a harmful anachronism and need to be abolished.

The HTTP protocol is defined by RFC-2616.  Even though RFC-2616 says that CRLF
line endings are required, that same spec recommends that all clients
also accept bare \\n line endings.  Most HTTP clients abide by this
recommendation, and so Fossil's refusal to play by the rules is harmless.  
And omitting those extra \\r bytes reduces bandwidth slightly.
The omission of unnecessary \\r characters is a feature of Fossil, not a bug.

~~~ pikchr
sin45 = sin(3.141592653/4)
C:  circle "CRLF" big big bold thick fit
C2: circle thick thick radius C.radius at C.c color red
    line thick thick from (C.x-sin45*C.radius,C.y-sin45*C.radius) \
                 to (C.x+sin45*C.radius,C.y+sin45*C.radius) color red
T1: text "CRLF-free" bold fit with .s at 1mm above C.n
T2: text "Zone" bold fit with .n at 1mm below C.s
    box ht dist(T1.s,T2.n)+lineht*1.2 wid C2.width+lineht*0.5 \
       fill yellow thick thick radius 3mm at C.c behind C
~~~

## How Does This Affect Me

It doesn't.  You won't notice that Fossil omits unnecessary CRs unless you look
at a hex-dump of the HTTP protocol that it generates.  This documentation page
exists only so that in case somebody does notice, they won't think the omission
of CRs is a bug.

## My Boss Says I Can Only Host Standards-Compliant Software.

If you recompile Fossil using the "`-DSEND_CR=1`" compile-time option, then it
will generate all extra CRs required by HTTP and SMTP.

## Why Not Just Make Fossil Standards-Compliant By Default?

Because the standard is wrong.  Requiring CRLF as a line-ending is silly.
It wastes bandwidth.  It is vexation to programmers that have to deal with the
extra CRs.  It is an anachronism based on hardware constraints in 1950s-era
teleprinters.  CRLF needs to be abolished.  And the only way that will happen
is if the users revolt.
