



## DMA issue notes

DMA to the DAC is stalled
  - for long enough to miss samples even at the slow 48kHz (or even lower)
when
  - the program does a read of a Read-Synchronized register
  - of a peripheral on the same Bridge (C)
  - and
    - the peripheral is clocked by something other than GCLK_GEN0
    - and no read request synchronization steps
so, such reads must be done either
  - to peripherals that are clocked by GLCK_GEN0
  - or, with read request synchronization

Demonstrated this with TC3 and it's COUNT16.COUNT register (which is read-sync)
But it appears to be the same issue with the FreeTouch code, which reads three
  such registers during measure():
    FREQCONTROL, CONVCONTROL, & RESULT

? Haven't test if Write Synchronized registers have the same issue
  - if so, then the FreeTouch code writes a lot of registers in measure_raw()
    - even if it doesn't really have to reconfigure the PTC on each measurement
  - and will need to check which are write sync., and are really needed

A single event seems induce about 185µs of stall
20 events in a row induce about 3.7ms of stall (which is 185µs x 20),
  but some DMA transfers do get by in that time


## Computing discrete exponentials

Often we want to slew a parameter from x to y over some period of time.
We want this slew to be smooth, and exponentially approaching.
And we want this to be easy to calculate.

A simple approach is to use this code on each step of time:

    x = x + (y - x)*r;

If computed with fixed point arithmetic, this has the downside that as x
approachs y, (y - x) is small, and (y - x)*q may well round to zero, and thus
x never achieves y.  This can be avoided by rewriting the code thus:

    x = y - (y - x)*q;    // where q = (1 - r)

Now the question is how to choose q (or r):

In closed form, the code above is this recurrence relation:

    x(0) = x
    x(n+1) = y - (y - x(n))*q

This can be solved in closed form[1] as:

    x(n) = (x - y)*q^n + y

A full specification of the slew one wants is to say that you want x to be
just  shy some % c of the final value after t time, given some stepping rate S:

    q^n = c, where n = t*S

For example, to reach within -24dB after 2ms stepping at 48kHz:

    q^(0.002 * 48,000) = 10^(-24/20)
    q^96 = 0.063096
    q = 96 root 0.063096
    q = 0.97162795158


[1] Input this into Wolfram Alpha to get the solution:

    v(0)=x, v(n+1)=y - (y - v(n))q
