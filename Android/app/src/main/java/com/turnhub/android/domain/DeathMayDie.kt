package com.turnhub.android.domain

/**
 * Cthulhu: Death May Die's investigators, by the box they come in, for the
 * lobby's picker. Names only: each player still plays from their own
 * dashboard. Compiled from the community Death May Die wiki
 * (cthulhu-death-may-die.fandom.com, "Investigators") and checked against
 * Make Craft Game's character guides; a name missing here can be typed in.
 */
data class InvestigatorBox(val title: String, val investigators: List<String>)

object DeathMayDie {
    val boxes: List<InvestigatorBox> = listOf(
        InvestigatorBox("Season 1 (core box)", listOf(
            "Ahmed Yasin", "Borden", "Elizabeth Ives", "Fatima Safar", "John Morgan", "Lord Adam Benchley",
            "Rasputin", "Sergeant Ian Welles", "Sister Beth", "The Kid",
        )),
        InvestigatorBox("Season 2", listOf(
            "Adilah Vidal", "Alex Parker", "Ariele Venturi", "Father Luke", "Halia Hovath", "Machinist's Mate Tony Boyle",
            "Margarethe Richter", "Mary Diaz", "Sam Wood", "Sergeant Mario Braghieri",
        )),
        InvestigatorBox("Season 3 (Fear of the Unknown)", listOf(
            "Agatha May", "Huikong", "Jakub Peters", "Julien Dulik", "Leon Barnes", "Mike Kozlov",
            "Ruth Weber", "Sally Withers", "Sandra Atkinson", "Stella Fontaine",
        )),
        InvestigatorBox("Season 4", listOf(
            "Aniko Biko", "Bruno Vitale", "Gary West", "John Fernsby", "Rosario Guerra", "Sylvia Doyle",
        )),
        InvestigatorBox("Season 5", listOf(
            "Anna & Belle", "Denise Rousseau", "Godfrey Bourne", "Harry Larson", "Jaime Gusmão",
            "Julius Johns", "Katya Morozova", "Nathan Wayne", "Simone Sinclair", "Yuqi Wan",
        )),
        InvestigatorBox("Season 6", listOf(
            "Aisha Adams", "Artemisia Greco", "Freddy", "Lilian Gray", "Matteo Torre", "Ralph Hardee",
        )),
        InvestigatorBox("Unspeakable Box", listOf(
            "Al Capone", "Albert Einstein", "Ernest Hemingway", "Fleur Dubois", "Josephine Leclerc", "Julia Hollister",
            "Maki Smith", "Marcelo 'Gonk' Gonçalves", "Marjorie 'Margie' Westmoreland", "Maxim Grey", "Meryl Starling",
            "Olivia Duchamp", "'Pops' Duperon", "Professor Vincent Haas", "Richard Irons", "Roxie Mitchell Williams",
            "Tina Macshane", "Walter Bagget",
        )),
        InvestigatorBox("Unknowable Box", listOf(
            "Agnes Fortin", "Amelie Crowe", "Amir Sesay", "Antonio Senatore", "Archie Fort", "Buck Hall",
            "Charles Hollar", "Craig Adamson", "Don Hayworth", "Dorothy Parker", "Eve Blanchet", "George Rodriguez",
            "Harry Houdini", "Ikeda Saito", "Isadora Rocha", "James Dalton", "Jim Cannon", "Karen Mendel",
            "Martin Horner", "Michelle Hart", "Robert Gray", "Ryan Martell", "Salvador Dalí", "Steve Wainwright",
            "Taylor Renner", "Tom Gilman", "Vanessa Ortiz",
        )),
        InvestigatorBox("Unimaginable Box", listOf(
            "Abdul Alhazred", "Azelia Johnson", "Boris Karloff", "Cab Calloway", "Dr. Herbert West", "Faust",
            "Frida Kahlo", "Hu Die", "Joan Crawford", "Kenji Takahashi", "Larry Carmine", "Ludwig Bauer",
            "Marlene Dietrich", "Max Schreck", "Nate", "Nikola Tesla", "Remy Dakanotic", "Rita Marunchak",
            "Santos-Dumont", "Tarsila do Amaral", "Virginia Woolf", "Zelda Fitzgerald",
        )),
        InvestigatorBox("Comic Book Extras", listOf(
            "Allan Jenkins", "Annabelle Laveau", "Ethan", "Ezra", "Jack Muñoz", "Lysa", "Olmstead", "Rocco",
            "Tamara", "Veronique", "Virginia Lyman",
        )),
        InvestigatorBox("Promos", listOf("Eddie the Head", "Scarlett Hayes")),
    )

    /** Every investigator once, in box order. */
    val all: List<String> get() = boxes.flatMap { it.investigators }

    /** The box [name] comes in; null for a name typed in. */
    fun boxOf(name: String): String? = boxes.firstOrNull { name in it.investigators }?.title

    /**
     * Who has taken their turn this round: [acted] (player indices) once each
     * living player has, the round is over and the next starts empty.
     */
    fun roundOver(acted: Set<Int>, living: List<Int>): Boolean = living.isNotEmpty() && living.all { it in acted }
}
