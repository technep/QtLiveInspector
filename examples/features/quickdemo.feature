Feature: quickdemo basics
  Background:
    Given the application is running

  Scenario: clicking increments the counter
    When I click "clickButton"
    And I click "clickButton"
    Then the "text" property of "clickButton" should be "Clicked 2 times"

  Scenario Outline: slider updates the label
    When I set the "value" property of "volumeSlider" to <v>
    Then the "text" property of "volumeLabel" should become <v> within 2 seconds

    Examples:
      | v  |
      | 10 |
      | 90 |
