Fixed FCDS draining a replaced or removed filter chain once per in-place updated version of a
listener instead of once per listener, which grew the number of draining filter chains with each
listener update.
