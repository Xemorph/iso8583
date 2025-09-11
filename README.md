# ISO8583
A repository full of stuff which relates to ISO8583

The website `https://paymentcardtools.com/` contains a lot of useful tools
who are helpful working with ISO8583 or ISO8583-based messages. Give it a shot :)

There is a software available called `jPOS` which also contains some ISO8583 stuff;
especially the source code, written in `Java`, contains an ISO8583-Parser and POS things.
`jPOS` is actually for financial systems and completly free.

It is possible to get access to the official ISO8583 technical specification. But the
specs of VISA and Mastercard are only based-on ISO8583 and there is no way to get as a non-company
access to these spec. documents.
`jPOS` have in their source code a raw documentation about the VISA & Mastercard transaction messages but
they are missing the small details.

I noticed that `jPOS` is also lacking off one very important thing, which is MerchantCategoryCodes.
In the internet you can find various tools which allows you to search for MCCs but I found this
[paper](https://www.citibank.com/tts/solutions/commercial-cards/assets/docs/govt/Merchant-Category-Codes.pdf) from the `citi bank`

---

### Abstraction
```
+ISOMessage // Root
|-- ISOComponent // Leaf
|-- ISOComponent
|-+ ISOMessage // Nested
| |-- ISOComponent
| |-- ISOComponent
| |-- ISOComponent
|-- ISOComponent

etc.
```