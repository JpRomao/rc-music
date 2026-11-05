/*
 * Original RC Music offline test fixture. No downloaded site/player code.
 * The wrapper and alr marker exercise EJS 0.8.0's player preprocessing.
 * n reverses Unicode codepoints; sig rotates them left by one.
 */
(function () {
    "use strict";

    function RcmusicSyntheticUrl() {
        this.values = Object.create(null);
    }

    RcmusicSyntheticUrl.prototype.set = function (key, value) {
        this.values[key] = value;
    };

    RcmusicSyntheticUrl.prototype.get = function (key) {
        return this.values[key];
    };

    RcmusicSyntheticUrl.prototype.applyTransforms = function () {
        var n = this.get("n");
        if (typeof n === "string") {
            this.set("n", Array.from(n).reverse().join(""));
        }

        var signature = this.get("s");
        if (typeof signature === "string") {
            var letters = Array.from(decodeURIComponent(signature));
            if (letters.length > 0) {
                letters.push(letters.shift());
            }
            this.set("s", encodeURIComponent(letters.join("")));
        }
    };

    function rcmusicFactory(source, signatureKey, encodedSignature) {
        var url = new RcmusicSyntheticUrl();
        url.set("alr", "yes");
        if (typeof encodedSignature === "string") {
            url.set(signatureKey, encodedSignature);
        }
        return url;
    }
}).call(this);
