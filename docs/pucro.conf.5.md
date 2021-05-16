# pucro.conf(5) -- pucrod configuration files

## SYNOPSIS

/etc/pucro.conf

## DESCRIPTION

This configuration file controls the button mapping rules that pucrod follows.

## SYNTAX

Configuration files are formatted as such:

```
rule {
  buttons = { button1, button2, ... }
  users = { user1, user2... }
  action = "echo 'this is the action'"
}

rule {
  ...
}
```

The file consists of a sequence of `rule` blocks, each with the following attributes
inside:

- **buttons** is a comma-separated list of buttons that will trigger this rule.
- **users** is a comma-separated list of usernames that can trigger this rule.
- **action** is a quoted shell command that will be run when any of the given users press
  one of the given buttons.

## BUTTON NAMES

In order to determine what button names to use, run `libinput debug-events` and click the
button that you want to determine the name of. You will see output similar to the
following:

```
event4   POINTER_BUTTON   +6.219s	BTN_EXTRA (276) pressed, seat count: 1
```

This means the button pressed was `BTN_EXTRA`. The button name to use with pucro is this
name, but lowercased and without the `BTN_` prefix. In this case, the button name would
be `extra`.

## EXAMPLES

This will run the `gtk3-demo` GUI application whenever the side *or* extra buttons are
pressed by the user `username`:

```
rule {
  buttons = { side, extra }
  users = { username }
  action = "gtk3-demo"
}
```

## SEE ALSO

pucrod.service(8)
