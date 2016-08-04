[Starting with a tape full of 0s, the optimizer should know to delete all of
 the [-] loops in this code. It should be able to do so without creating
 no-ops.
 The . operator here is used to provide a boundary, intended to prevent the
 optimizer from collapsing the separate tests together.
]
.
[-]>[-]>[-]>[-]>
[-]<[-]<[-]<[-]<
.
>[-]<
.
[-]>[-]<
.
>[>-<>+<]<
.

This test must be last because it adds and subtracts
>[-]+<>-<
.

Print "OK"
+++++[>++++<-]>[<++++>-]<-.----.
>++++++++++.
