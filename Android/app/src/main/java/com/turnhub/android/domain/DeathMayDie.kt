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
    /** The dashboard's stress track holds 5 (owner, 2026-10-19). */
    const val MAX_STRESS = 5

    /** Five wounds is the most an investigator survives; the sixth kills them (owner, 2026-10-19). */
    const val DEATH_WOUNDS = 6

    val boxes: List<InvestigatorBox> = listOf(
        InvestigatorBox("Season 1 (core box)", listOf(
            "Ahmed Yasin", "Borden", "Elizabeth Ives", "Fatima Safar", "John Morgan", "Lord Adam Benchley",
            "Rasputin", "Sergeant Ian Welles", "Sister Beth", "The Kid",
        )),
        InvestigatorBox("Season 2", listOf(
            "Adilah Vidal", "Alex Parker", "Ariele Venturi", "Father Luke", "Hailia Hovath", "Machinist's Mate Tony Boyle",
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

    /**
     * The community wiki's picture of an investigator, where it has one (its file path under
     * the wiki's image host): box art for Season 1, a photo or scan of the dashboard after.
     */
    private val art: Map<String, String> = mapOf(
        "Adilah Vidal" to "2/2a/AdilahDashboardFront.png",
        "Agatha May" to "6/67/Agatha.jpg",
        "Ahmed Yasin" to "9/97/Achmed.jpg",
        "Alex Parker" to "3/31/ParkerDashboardFront.png",
        "Aniko Biko" to "e/e3/Anika.jpg",
        "Ariele Venturi" to "5/50/ArieleDashboardFront.png",
        "Borden" to "5/5d/Borden.jpg",
        "Bruno Vitale" to "c/c2/Bruno.jpg",
        "Elizabeth Ives" to "2/26/Elizabeth.jpg",
        "Father Luke" to "2/2b/LukeDashboardFront.png",
        "Fatima Safar" to "7/7a/Fatima.jpg",
        "Gary West" to "b/bd/Gary.jpg",
        "Hailia Hovath" to "8/80/HaliaDashboardFront.png",
        "Huikong" to "e/e9/Huikong.jpg",
        "Jakub Peters" to "4/47/Peters.jpg",
        "John Fernsby" to "d/dd/John.jpg",
        "John Morgan" to "6/6f/Morgan.jpg",
        "Julien Dulik" to "5/52/Julian.jpg",
        "Leon Barnes" to "1/18/Leon.jpg",
        "Lord Adam Benchley" to "e/e5/Adam.jpg",
        "Machinist's Mate Tony Boyle" to "f/fe/TonyDashboardFront.png",
        "Margarethe Richter" to "e/e6/MargaretheDashboardFront.png",
        "Mary Diaz" to "4/43/MaryDashboardFront.png",
        "Mike Kozlov" to "9/90/Mike.jpg",
        "Rasputin" to "8/8e/Rasputin.jpg",
        "Rosario Guerra" to "9/99/Rosario.jpg",
        "Ruth Weber" to "c/c5/Ruth.jpg",
        "Sally Withers" to "7/7c/Sally.jpg",
        "Sam Wood" to "f/fd/SamDashboardFront.png",
        "Sandra Atkinson" to "a/ac/Sandra.jpg",
        "Sergeant Ian Welles" to "3/3b/Ian.jpg",
        "Sergeant Mario Braghieri" to "9/9b/MarioDashboardFront.png",
        "Sister Beth" to "0/06/Beth.jpg",
        "Stella Fontaine" to "f/fa/Stella.jpg",
        "Sylvia Doyle" to "7/7a/Sylvia.jpg",
        "The Kid" to "a/a6/Kid.jpg",
    )

    /** Where to download [name]'s picture for their tile; null when the wiki has none. */
    fun artUrl(name: String): String? = art[name]?.let { "$WIKI_IMAGES/$it/revision/latest/scale-to-width-down/1000" }

    /** A dashboard picture is wide: its portrait is the left-hand panel, cropped as left, top, right, bottom. */
    val dashboardPortrait = listOf(.15f, .13f, .44f, .80f)

    private const val WIKI_IMAGES = "https://static.wikia.nocookie.net/cthulhu-death-may-die/images"

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
