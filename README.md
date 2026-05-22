# Magic Counting

Magic Counting is a Qt Widgets double-accounting app. The main window edits account transactions in a table. Each transaction has a date, a source account, an amount, the other party, one or more target account splits, a memo, and computed posting columns for every configured account.

## Build

```sh
cmake -S . -B build
cmake --build build
./build/magiccounting
```

The project targets C++17 and Qt 5 Widgets.

## Data Files

The app stores editable text data as JSON arrays in a data folder:

- `accounts.json`
- `parties.json`
- `transactions.json`

Use `File -> Open Data Folder...` to load another set of these files. Transaction edits are written back to `transactions.json` after each valid row edit. Account and party configuration is saved when accepting the corresponding edit dialog.

Click or double-click the `Target accounts` cell to edit split rows in a popup. The popup keeps `OK` disabled until the target split total equals the transaction `Amount`. The source account posting is shown as a negative amount, and negative postings are rendered in red.

The first row is an opening-balance row. Enter each account's initial balance directly in that row; values are saved in `accounts.json`. The app inserts a generated balance row after the last transaction of each month, showing each account's balance after that month.
